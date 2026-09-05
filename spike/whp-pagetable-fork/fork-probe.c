/* fork-probe: fork as a page-table copy inside one WHP partition.
 *
 * Shape B of substrate H (one kernel process, a page-table root per Linux process) keeps every Linux process
 * in one partition as its own page-table root, and forks by copying the
 * tables, marking every writable leaf read-only in both, and copying a page
 * when a write faults. The host is the kernel, so the fault has to reach the
 * host: WHP can route a guest #PF to the host as an exception exit instead of
 * delivering it to a guest IDT. Nothing here had been measured: whether the
 * exception exit works, what the copy costs at realistic sizes, what one
 * copy-on-write fault costs end to end, what switching a vCPU between roots
 * costs, and whether the isolation is right.
 *
 * Six questions, key=value on stdout; measure.sh states the verdict. Built with
 * Cygwin's gcc against w32api's winhvplatform, like spike/whp.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhvplatform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE "fork-probe 1.0"

#define PAGE   0x1000ULL
#define MB     0x100000ULL

/* Guest physical memory is one host buffer at GPA 0. The first four
 * megabytes are control: GDT, TSS, code, and a stack nothing pushes to. Frames
 * for page tables and data come from a bump allocator above that. */
#define GPA_GDT    0x4000
#define GPA_TSS    0x5000
#define GPA_CODE   0x6000
#define GPA_STACK  0x8000
#define CONTROL    (4 * MB)
#define FRAME_BASE CONTROL

#define EP_WRITE   (GPA_CODE + 0x000)
#define EP_READ    (GPA_CODE + 0x100)

/* Two regions a process maps, chosen so the copy is measured at two sizes. */
#define VA_SMALL   0x10000000ULL
#define SMALL      (64 * MB)
#define VA_LARGE   0x40000000ULL
#define LARGE      (512 * MB)

/* PTE bits. Bit 9 marks a leaf as copy-on-write; bit 10 marks a leaf the
 * fork leaves alone (control memory, shared and never written by the guest). */
#define PTE_P      0x001ULL
#define PTE_RW     0x002ULL
#define PTE_US     0x004ULL
#define PTE_COW    0x200ULL
#define PTE_NOCOW  0x400ULL
#define PTE_ADDR   0x000ffffffffff000ULL

static int verbose;
static WHV_PARTITION_HANDLE part;
static UINT8 *phys;
static UINT64 phys_size;
static UINT64 next_frame;          /* bump allocator, in bytes from phys */
static UINT16 *refcount;           /* per frame */
static UINT64 frames_copied_for_cow;

static void emit(const char *k, const char *fmt, ...)
{
	va_list ap;
	printf("%s=", k);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

static void trace(const char *fmt, ...)
{
	va_list ap;
	if (!verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static LARGE_INTEGER freq;
static UINT64 now_ns(void)
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (UINT64) ((double) t.QuadPart * 1e9 / (double) freq.QuadPart);
}

static int cmp_u64(const void *a, const void *b)
{
	UINT64 x = *(const UINT64 *) a, y = *(const UINT64 *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static void stats(const char *prefix, UINT64 *lat, size_t n)
{
	char key[80];
	if (n == 0) { sprintf(key, "%s_samples", prefix); emit(key, "0"); return; }
	qsort(lat, n, sizeof *lat, cmp_u64);
	sprintf(key, "%s_median_ns", prefix); emit(key, "%llu", (unsigned long long) lat[n / 2]);
	sprintf(key, "%s_p99_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[(n * 99) / 100]);
	sprintf(key, "%s_min_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[0]);
	sprintf(key, "%s_samples", prefix);   emit(key, "%llu", (unsigned long long) n);
}

/* ---- frames and page tables ------------------------------------------- */

static UINT64 alloc_frame(void)
{
	UINT64 f = next_frame;
	if (f + PAGE > phys_size) { fprintf(stderr, "fork-probe: out of guest frames\n"); exit(3); }
	next_frame += PAGE;
	memset(phys + f, 0, PAGE);
	refcount[f / PAGE] = 1;
	return f;
}

static UINT64 *table(UINT64 frame) { return (UINT64 *) (phys + frame); }

/* Walk or build down to the PTE for va under root; returns a pointer into
 * guest memory. With create=0 returns NULL where a level is absent. */
static UINT64 *pte_for(UINT64 root, UINT64 va, int create)
{
	UINT64 *t = table(root);
	int level;
	for (level = 3; level >= 1; level--) {
		unsigned idx = (unsigned) ((va >> (12 + 9 * level)) & 0x1ff);
		if (!(t[idx] & PTE_P)) {
			if (!create) return NULL;
			t[idx] = alloc_frame() | PTE_P | PTE_RW | PTE_US;
		}
		t = table(t[idx] & PTE_ADDR);
	}
	return &t[(va >> 12) & 0x1ff];
}

static void map_range(UINT64 root, UINT64 va, UINT64 gpa, UINT64 len, UINT64 flags)
{
	UINT64 off;
	for (off = 0; off < len; off += PAGE)
		*pte_for(root, va + off, 1) = (gpa + off) | flags;
}

/* A new process: control memory identity-mapped and left out of copy-on-write,
 * nothing else. */
static UINT64 new_root(void)
{
	UINT64 root = alloc_frame();
	map_range(root, 0, 0, CONTROL, PTE_P | PTE_RW | PTE_NOCOW);
	return root;
}

/* Map fresh frames behind [va, va+len) and fill each with a pattern that
 * names its page, so a later read can say which frame it came from. */
static void populate(UINT64 root, UINT64 va, UINT64 len, UINT64 tag)
{
	UINT64 off;
	for (off = 0; off < len; off += PAGE) {
		UINT64 f = alloc_frame();
		*(UINT64 *) (phys + f) = tag ^ (va + off);
		*pte_for(root, va + off, 1) = f | PTE_P | PTE_RW | PTE_US;
	}
}

/* The fork. Copies the four-level tree; every writable, copy-on-write-eligible
 * leaf loses its write bit in both trees and gains the COW mark, and its
 * frame's count goes up. Returns the child's root. */
static UINT64 copied_tables;

static UINT64 clone_level(UINT64 frame, int level)
{
	UINT64 *src = table(frame), *dst;
	UINT64 nf = alloc_frame();
	unsigned i;
	dst = table(nf);
	copied_tables++;
	for (i = 0; i < 512; i++) {
		UINT64 e = src[i];
		if (!(e & PTE_P)) continue;
		if (level > 1) {
			dst[i] = (e & ~PTE_ADDR) | clone_level(e & PTE_ADDR, level - 1);
		} else if (e & PTE_NOCOW) {
			dst[i] = e;
		} else {
			UINT64 f = e & PTE_ADDR;
			if (e & PTE_RW) {
				e = (e & ~PTE_RW) | PTE_COW;
				src[i] = e;
			}
			dst[i] = e;
			refcount[f / PAGE]++;
		}
	}
	return nf;
}

static UINT64 as_clone(UINT64 root)
{
	copied_tables = 0;
	return clone_level(root, 4);
}

/* Tear a clone down: every leaf reference it held is dropped. Frames are
 * not recycled (a bump allocator has no free), which is fine for a probe. */
static void release_level(UINT64 frame, int level)
{
	UINT64 *t = table(frame);
	unsigned i;
	for (i = 0; i < 512; i++) {
		UINT64 e = t[i];
		if (!(e & PTE_P)) continue;
		if (level > 1) release_level(e & PTE_ADDR, level - 1);
		else if (!(e & PTE_NOCOW)) refcount[(e & PTE_ADDR) / PAGE]--;
	}
}

static void as_release(UINT64 root) { release_level(root, 4); }

/* Resolve a write fault at va under root: copy the frame if shared, take it
 * if this is the last reference. Returns 0 when the fault was not ours. */
static int cow_resolve(UINT64 root, UINT64 va)
{
	UINT64 *pte = pte_for(root, va, 0);
	UINT64 f;
	if (!pte || !(*pte & PTE_P) || !(*pte & PTE_COW)) return 0;
	f = *pte & PTE_ADDR;
	if (refcount[f / PAGE] > 1) {
		UINT64 nf = alloc_frame();
		memcpy(phys + nf, phys + f, PAGE);
		refcount[f / PAGE]--;
		frames_copied_for_cow++;
		*pte = nf | (*pte & ~(PTE_ADDR | PTE_COW)) | PTE_RW;
	} else {
		*pte = (*pte & ~PTE_COW) | PTE_RW;
	}
	return 1;
}

/* ---- the guest ----------------------------------------------------------- */

static void put64(UINT64 gpa, UINT64 v) { memcpy(phys + gpa, &v, 8); }

/* mov %rax,(%rbx); hlt; jmp back -- and the load twin. Ring 0 with CR0.WP set,
 * so a store to a read-only page faults exactly as a user store would. */
static const UINT8 code_write[] = { 0x48, 0x89, 0x03, 0xf4, 0xeb, 0xfa };
static const UINT8 code_read[]  = { 0x48, 0x8b, 0x03, 0xf4, 0xeb, 0xfa };

static void build_control(void)
{
	memset(phys, 0, CONTROL);
	put64(GPA_GDT + 0x08, 0x00af9b000000ffffULL);
	put64(GPA_GDT + 0x10, 0x00cf93000000ffffULL);
	put64(GPA_GDT + 0x30, 0x0000890000000067ULL | ((UINT64)(GPA_TSS & 0xffffff) << 16)
	                      | ((UINT64)((GPA_TSS >> 24) & 0xff) << 56));
	put64(GPA_TSS + 0x04, GPA_STACK);
	memcpy(phys + EP_WRITE, code_write, sizeof code_write);
	memcpy(phys + EP_READ, code_read, sizeof code_read);
}

static WHV_X64_SEGMENT_REGISTER seg(UINT16 sel, UINT8 type, int lng, int def32)
{
	WHV_X64_SEGMENT_REGISTER s;
	memset(&s, 0, sizeof s);
	s.Limit = 0xffffffff; s.Selector = sel; s.SegmentType = type;
	s.NonSystemSegment = 1; s.Present = 1; s.Long = (UINT16) lng;
	s.Default = (UINT16) def32; s.Granularity = 1;
	return s;
}

static HRESULT enter(UINT64 root, UINT64 rip, UINT64 rbx, UINT64 rax)
{
	WHV_REGISTER_NAME n[24];
	WHV_REGISTER_VALUE v[24];
	WHV_X64_SEGMENT_REGISTER data = seg(0x10, 3, 0, 1);
	WHV_X64_SEGMENT_REGISTER code = seg(0x08, 0xb, 1, 0);
	int i = 0;

	memset(v, 0, sizeof v);
	n[i] = WHvX64RegisterCr0;    v[i++].Reg64 = 0x80010031ULL;    /* PE MP NE ET WP PG */
	n[i] = WHvX64RegisterCr3;    v[i++].Reg64 = root;
	n[i] = WHvX64RegisterCr4;    v[i++].Reg64 = 0x00000620ULL;
	n[i] = WHvX64RegisterEfer;   v[i++].Reg64 = 0x00000d01ULL;
	n[i] = WHvX64RegisterCs;     v[i++].Segment = code;
	n[i] = WHvX64RegisterSs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterDs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterEs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterFs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterGs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterTr;
	v[i].Segment.Base = GPA_TSS; v[i].Segment.Limit = 0x67; v[i].Segment.Selector = 0x30;
	v[i].Segment.SegmentType = 11; v[i].Segment.Present = 1; i++;
	n[i] = WHvX64RegisterLdtr;   i++;
	n[i] = WHvX64RegisterGdtr;   v[i].Table.Base = GPA_GDT; v[i].Table.Limit = 0x3f; i++;
	n[i] = WHvX64RegisterIdtr;   i++;
	n[i] = WHvX64RegisterRflags; v[i++].Reg64 = 0x2;
	n[i] = WHvX64RegisterRsp;    v[i++].Reg64 = GPA_STACK - 0x10;
	n[i] = WHvX64RegisterRip;    v[i++].Reg64 = rip;
	n[i] = WHvX64RegisterRbx;    v[i++].Reg64 = rbx;
	n[i] = WHvX64RegisterRax;    v[i++].Reg64 = rax;
	return WHvSetVirtualProcessorRegisters(part, 0, n, (UINT32) i, v);
}

/* Aim the vCPU: which loop, which address, which value, and optionally which
 * root. %rip is set every time so a read never runs the store loop left over
 * from the last write. */
static HRESULT set_regs(UINT64 root, UINT64 rip, UINT64 rbx, UINT64 rax, int with_cr3)
{
	WHV_REGISTER_NAME n[4] = { WHvX64RegisterRip, WHvX64RegisterRbx, WHvX64RegisterRax, WHvX64RegisterCr3 };
	WHV_REGISTER_VALUE v[4];
	memset(v, 0, sizeof v);
	v[0].Reg64 = rip; v[1].Reg64 = rbx; v[2].Reg64 = rax; v[3].Reg64 = root;
	return WHvSetVirtualProcessorRegisters(part, 0, n, with_cr3 ? 4 : 3, v);
}

static HRESULT set_cr3(UINT64 root)
{
	WHV_REGISTER_NAME n = WHvX64RegisterCr3;
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	v.Reg64 = root;
	return WHvSetVirtualProcessorRegisters(part, 0, &n, 1, &v);
}

static UINT64 get_reg(WHV_REGISTER_NAME name)
{
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	if (FAILED(WHvGetVirtualProcessorRegisters(part, 0, &name, 1, &v))) return ~0ULL;
	return v.Reg64;
}

static WHV_RUN_VP_EXIT_CONTEXT ex;
static int flush_after_cow = 1;

static UINT32 run(void)
{
	memset(&ex, 0, sizeof ex);
	if (FAILED(WHvRunVirtualProcessor(part, 0, &ex, sizeof ex))) return 0xffffffffu;
	return ex.ExitReason;
}

/* Run until a halt, resolving copy-on-write faults on the way. Reports how
 * many faults it took and whether anything else happened. */
static int run_resolving(UINT64 root, int *faults, UINT64 *host_ns)
{
	int n = 0;
	UINT64 hn = 0;
	for (;;) {
		UINT32 r = run();
		if (r == WHvRunVpExitReasonX64Halt) { if (faults) *faults = n; if (host_ns) *host_ns = hn; return 1; }
		if (r == WHvRunVpExitReasonException && ex.VpException.ExceptionType == 14) {
			UINT64 t0 = now_ns();
			static int first = 1;
			if (first) {
				/* what the exit carries, once: the fault address against CR2,
				 * the error code, and whether %rip still points at the store */
				first = 0;
				emit("q4_first_fault_parameter", "0x%llx", (unsigned long long) ex.VpException.ExceptionParameter);
				emit("q4_first_fault_cr2_register", "0x%llx", (unsigned long long) get_reg(WHvX64RegisterCr2));
				emit("q4_first_fault_error_code", "0x%x", (unsigned) ex.VpException.ErrorCode);
				emit("q4_first_fault_rip_at_store", "%d", ex.VpContext.Rip == EP_WRITE);
			}
			if (!cow_resolve(root, ex.VpException.ExceptionParameter)) {
				trace("unresolvable #PF at 0x%llx err 0x%x",
				      (unsigned long long) ex.VpException.ExceptionParameter, (unsigned) ex.VpException.ErrorCode);
				return 0;
			}
			if (flush_after_cow) set_cr3(root);   /* flushes the stale translation */
			hn += now_ns() - t0;
			n++;
			if (n > 4) { trace("fault loop"); return 0; }
			continue;
		}
		trace("exit %u", (unsigned) r);
		if (faults) *faults = n;
		return 0;
	}
}

static int guest_write(UINT64 root, UINT64 va, UINT64 val, int *faults, UINT64 *host_ns)
{
	set_regs(root, EP_WRITE, va, val, 1);
	return run_resolving(root, faults, host_ns);
}

static int guest_read(UINT64 root, UINT64 va, UINT64 *val)
{
	int ok;
	set_regs(root, EP_READ, va, 0, 1);
	ok = run_resolving(root, NULL, NULL);
	*val = ok ? get_reg(WHvX64RegisterRax) : ~0ULL;
	return ok;
}

/* ---- bring-up ------------------------------------------------------------- */

static int bring_up(void)
{
	WHV_CAPABILITY cap;
	WHV_PARTITION_PROPERTY prop;
	HRESULT hr;
	UINT32 w = 0;
	int ext = 0, bitmap_ok = 0;

	memset(&cap, 0, sizeof cap);
	if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeExtendedVmExits, &cap, sizeof cap, &w)))
		ext = cap.ExtendedVmExits.ExceptionExit;
	emit("q1_exception_exit_capability", "%d", ext);
	memset(&cap, 0, sizeof cap);
	if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeExceptionExitBitmap, &cap, sizeof cap, &w))) {
		emit("q1_exception_exit_bitmap", "0x%016llx", (unsigned long long) cap.ExceptionExitBitmap);
		bitmap_ok = (cap.ExceptionExitBitmap >> 14) & 1;
	}
	emit("q1_page_fault_exit_available", "%d", ext && bitmap_ok);
	if (!(ext && bitmap_ok)) return 0;

	hr = WHvCreatePartition(&part);
	if (FAILED(hr)) { emit("setup_create_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	memset(&prop, 0, sizeof prop);
	prop.ProcessorCount = 1;
	hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof prop);
	if (SUCCEEDED(hr)) {
		memset(&prop, 0, sizeof prop);
		prop.ExtendedVmExits.ExceptionExit = 1;
		hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeExtendedVmExits, &prop, sizeof prop);
	}
	if (SUCCEEDED(hr)) {
		memset(&prop, 0, sizeof prop);
		prop.ExceptionExitBitmap = 1ULL << 14;
		hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeExceptionExitBitmap, &prop, sizeof prop);
	}
	if (SUCCEEDED(hr)) hr = WHvSetupPartition(part);
	emit("q1_partition_with_pf_exit_hresult", "0x%08lx", (unsigned long) hr);
	if (FAILED(hr)) return 0;

	phys = VirtualAlloc(NULL, phys_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!phys) { emit("setup_phys", "0"); return 0; }
	refcount = calloc((size_t) (phys_size / PAGE), sizeof *refcount);
	if (!refcount) return 0;
	build_control();
	next_frame = FRAME_BASE;
	hr = WHvMapGpaRange(part, phys, 0, phys_size,
	                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
	if (FAILED(hr)) { emit("setup_map_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	hr = WHvCreateVirtualProcessor(part, 0, 0);
	if (FAILED(hr)) { emit("setup_vcpu_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	return 1;
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	UINT32 w = 0;
	UINT64 parent, child, child2, val, t0, lat[16];
	UINT64 tag_p = 0x5041524e54000000ULL, tag_c = 0x4348494c44000000ULL;
	int i, present, faults, cow_faults = 1024;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--cow-faults") && i + 1 < argc) { cow_faults = atoi(argv[++i]); continue; }
		fprintf(stderr, "fork-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	if (cow_faults < 16) cow_faults = 16;
	if (cow_faults > 8192) cow_faults = 8192;
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);
	/* control + both regions + tables (about 1.2 MB per 512 MB) + COW copies,
	 * with room for several forks' worth of tables */
	phys_size = CONTROL + SMALL + LARGE + SMALL + 32 * MB + (UINT64) cow_faults * 2 * PAGE;

	memset(&cap, 0, sizeof cap);
	present = SUCCEEDED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &w))
	          && cap.HypervisorPresent;
	emit("hypervisor_present", "%d", present);
	if (!present) return 0;
	if (!bring_up()) { emit("partition_ready", "0"); return 0; }
	emit("partition_ready", "1");

	/* q2. A process with 64 MB and 512 MB mapped, and the guest running in
	 * it: a write lands, a read returns it, no fault involved. */
	parent = new_root();
	populate(parent, VA_SMALL, SMALL, tag_p);
	populate(parent, VA_LARGE, LARGE, tag_p);
	emit("q2_parent_frames_mapped", "%llu", (unsigned long long) ((SMALL + LARGE) / PAGE));
	if (FAILED(enter(parent, EP_WRITE, VA_SMALL + 3 * PAGE, 0x77ULL))) { emit("q2_guest_runs", "0"); return 0; }
	i = run_resolving(parent, &faults, NULL);
	emit("q2_guest_write_no_fault", "%d", i && faults == 0);
	enter(parent, EP_READ, VA_SMALL + 3 * PAGE, 0);
	i = run_resolving(parent, &faults, NULL);
	val = get_reg(WHvX64RegisterRax);
	emit("q2_guest_read_back", "%d", i && val == 0x77ULL);
	emit("q2_guest_runs", "%d", i);
	*(UINT64 *) (phys + ((*pte_for(parent, VA_SMALL + 3 * PAGE, 0)) & PTE_ADDR)) = tag_p ^ (VA_SMALL + 3 * PAGE);

	/* q3. The fork, timed. The table copy for the 64 MB region alone is
	 * measured on a root that maps only it; the full parent (576 MB) after. */
	{
		UINT64 small_root = new_root(), roots[8];
		populate(small_root, VA_SMALL, SMALL, tag_p);
		for (i = 0; i < 5; i++) { t0 = now_ns(); roots[i] = as_clone(small_root); lat[i] = now_ns() - t0; }
		emit("q3_fork_64mb_tables_copied", "%llu", (unsigned long long) copied_tables);
		stats("q3_fork_64mb", lat, 5);
		for (i = 0; i < 5; i++) { t0 = now_ns(); roots[i] = as_clone(parent); lat[i] = now_ns() - t0; }
		emit("q3_fork_576mb_tables_copied", "%llu", (unsigned long long) copied_tables);
		stats("q3_fork_576mb", lat, 5);
		/* keep the last child; the others are released so the parent's frames
		 * are shared two ways, which is the state q5's "take" path needs */
		for (i = 0; i < 4; i++) as_release(roots[i]);
		child = roots[4];
	}

	/* q4. Copy-on-write faults from the child, one per page, timed end to
	 * end: the write, the exception exit, the copy, the CR3 reload, the
	 * resumed write, the halt. Then the same pages read back both sides. */
	{
		UINT64 *tot = malloc((size_t) cow_faults * sizeof *tot);
		UINT64 *host = malloc((size_t) cow_faults * sizeof *host);
		int ok = 0, one_fault = 0, wrong_parent = 0, wrong_child = 0, rip_moved = 0;
		UINT64 hn;
		frames_copied_for_cow = 0;
		enter(child, EP_WRITE, VA_LARGE, 0);
		for (i = 0; i < cow_faults; i++) {
			UINT64 va = VA_LARGE + (UINT64) i * PAGE;
			t0 = now_ns();
			if (guest_write(child, va, tag_c ^ va, &faults, &hn)) {
				ok++;
				if (faults == 1) one_fault++;
			}
			tot[i] = now_ns() - t0;
			host[i] = hn;
		}
		emit("q4_child_writes_completed", "%d", ok);
		emit("q4_child_writes_one_fault_each", "%d", one_fault);
		emit("q4_frames_copied", "%llu", (unsigned long long) frames_copied_for_cow);
		stats("q4_cow_fault_round_trip", tot, (size_t) cow_faults);
		stats("q4_cow_host_work", host, (size_t) cow_faults);
		/* isolation: the parent's frames still say parent; the child's say child */
		for (i = 0; i < cow_faults; i++) {
			UINT64 va = VA_LARGE + (UINT64) i * PAGE;
			UINT64 pf = *pte_for(parent, va, 0) & PTE_ADDR, cf = *pte_for(child, va, 0) & PTE_ADDR;
			if (*(UINT64 *) (phys + pf) != (tag_p ^ va)) wrong_parent++;
			if (*(UINT64 *) (phys + cf) != (tag_c ^ va)) wrong_child++;
			if (pf == cf) wrong_child++;
		}
		emit("q4_parent_frames_intact", "%d", wrong_parent == 0);
		emit("q4_child_frames_private", "%d", wrong_child == 0);
		/* and through the guest itself, both roots, a sample of pages */
		{
			int gp = 0, gc = 0;
			for (i = 0; i < cow_faults; i += cow_faults / 16) {
				UINT64 va = VA_LARGE + (UINT64) i * PAGE;
				if (guest_read(parent, va, &val) && val == (tag_p ^ va)) gp++;
				if (guest_read(child, va, &val) && val == (tag_c ^ va)) gc++;
			}
			emit("q4_guest_reads_parent_sampled", "%d", gp);
			emit("q4_guest_reads_child_sampled", "%d", gc);
			emit("q4_guest_reads_samples", "%d", 16);
		}
		(void) rip_moved;

		/* q4b. The same path without the CR3 reload after the copy. If the
		 * re-executed store completes in one fault, the hypervisor is not
		 * holding the stale read-only translation and the reload is waste. */
		{
			int n2 = cow_faults / 4, ok2 = 0, one2 = 0, wrong2 = 0;
			flush_after_cow = 0;
			for (i = 0; i < n2; i++) {
				UINT64 va = VA_LARGE + (UINT64) (cow_faults + i) * PAGE;
				t0 = now_ns();
				if (guest_write(child, va, tag_c ^ va, &faults, &hn)) {
					ok2++;
					if (faults == 1) one2++;
				}
				tot[i] = now_ns() - t0;
			}
			flush_after_cow = 1;
			for (i = 0; i < n2; i++) {
				UINT64 va = VA_LARGE + (UINT64) (cow_faults + i) * PAGE;
				UINT64 cf = *pte_for(child, va, 0) & PTE_ADDR;
				if (*(UINT64 *) (phys + cf) != (tag_c ^ va)) wrong2++;
			}
			emit("q4b_no_flush_writes_completed", "%d", ok2);
			emit("q4b_no_flush_one_fault_each", "%d", one2);
			emit("q4b_no_flush_wrong_values", "%d", wrong2);
			emit("q4b_no_flush_samples_wanted", "%d", n2);
			stats("q4b_no_flush_round_trip", tot, (size_t) n2);

			/* q4c. The same call path onto pages the child already owns: no
			 * fault, so the difference from q4 is what the fault itself costs. */
			for (i = 0; i < n2; i++) {
				UINT64 va = VA_LARGE + (UINT64) i * PAGE;
				t0 = now_ns();
				guest_write(child, va, tag_c ^ va, &faults, &hn);
				tot[i] = now_ns() - t0;
				if (faults) wrong2++;
			}
			emit("q4c_private_write_faults", "%d", wrong2);
			stats("q4c_private_write_round_trip", tot, (size_t) n2);
		}
		free(tot); free(host);
	}

	/* q5. The other two copy-on-write paths: the parent writing a page the
	 * child never touched (both still share it; the parent copies), and the
	 * parent writing a page the child already copied (refcount one; the
	 * parent takes it without a copy). Then a second fork of the child. */
	{
		UINT64 va_shared = VA_SMALL + 10 * PAGE, va_taken = VA_LARGE + 2 * PAGE;
		UINT64 before = frames_copied_for_cow, hn;
		int f1 = -1, f2 = -1;
		enter(parent, EP_WRITE, va_shared, 0);
		guest_write(parent, va_shared, 0xA5A5ULL, &f1, &hn);
		emit("q5_parent_write_shared_faults", "%d", f1);
		emit("q5_parent_write_shared_copied", "%d", frames_copied_for_cow == before + 1);
		guest_read(child, va_shared, &val);
		emit("q5_child_still_sees_original", "%d", val == (tag_p ^ va_shared));
		before = frames_copied_for_cow;
		guest_write(parent, va_taken, 0x5A5AULL, &f2, &hn);
		emit("q5_parent_write_taken_faults", "%d", f2);
		emit("q5_parent_write_taken_no_copy", "%d", frames_copied_for_cow == before);
		guest_read(child, va_taken, &val);
		emit("q5_child_unaffected_by_taken", "%d", val == (tag_c ^ va_taken));
		guest_read(parent, va_taken, &val);
		emit("q5_parent_reads_own_write", "%d", val == 0x5A5AULL);

		child2 = as_clone(child);
		guest_read(child2, VA_LARGE + 7 * PAGE, &val);
		emit("q5_grandchild_sees_child_value", "%d", val == (tag_c ^ (VA_LARGE + 7 * PAGE)));
		guest_write(child2, VA_LARGE + 7 * PAGE, 0x1234ULL, &f1, &hn);
		guest_read(child, VA_LARGE + 7 * PAGE, &val);
		emit("q5_child_unaffected_by_grandchild", "%d", val == (tag_c ^ (VA_LARGE + 7 * PAGE)));
	}

	/* q6. Switching a vCPU between roots. Reads with no fault: same root
	 * every run, then alternating roots every run; the difference is the
	 * CR3 reload and whatever the hypervisor does about it. */
	{
		enum { n = 2000 };
		UINT64 *same = malloc(n * sizeof *same), *alt = malloc(n * sizeof *alt);
		UINT64 va = VA_SMALL + 20 * PAGE;
		int bad = 0;
		enter(parent, EP_READ, va, 0);
		for (i = 0; i < n; i++) {
			t0 = now_ns();
			set_regs(parent, EP_READ, va, 0, 0);
			if (run() != WHvRunVpExitReasonX64Halt) bad++;
			same[i] = now_ns() - t0;
		}
		for (i = 0; i < n; i++) {
			UINT64 root = (i & 1) ? child : parent;
			t0 = now_ns();
			set_regs(root, EP_READ, va, 0, 1);
			if (run() != WHvRunVpExitReasonX64Halt) bad++;
			alt[i] = now_ns() - t0;
		}
		emit("q6_runs_not_halting", "%d", bad);
		stats("q6_same_root", same, n);
		stats("q6_alternating_roots", alt, n);
		free(same); free(alt);
	}

	emit("frames_used_mb", "%llu", (unsigned long long) (next_frame / MB));
	WHvDeleteVirtualProcessor(part, 0);
	WHvUnmapGpaRange(part, 0, phys_size);
	WHvDeletePartition(part);
	return 0;
}
