/* Does the Windows Hypervisor Platform give this host a substrate for
 * proposal 0011's H, and what does one guest-to-host exit cost?
 *
 * Eight answers on stdout as key=value, one per line, in the order the
 * transcript reads them. Everything here is Win32; there is no POSIX in this
 * file, the same split the map-and-jump spike keeps between its two probes.
 *
 * The guest is built by hand rather than assembled: a few dozen bytes of
 * machine code, four page-table pages, a GDT and a TSS, laid into one host
 * allocation that is mapped as guest physical memory at GPA 0. Guest physical
 * equals guest virtual for the first sixteen megabytes, which is what the
 * kernel of section 4.3 would arrange anyway.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhvplatform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE "whp-probe 1.0"

/* Guest physical layout. The first page stays zero so a null dereference in
 * guest code is loud rather than quiet. */
#define GPA_PML4   0x1000
#define GPA_PDPT   0x2000
#define GPA_PD     0x3000
#define GPA_GDT    0x4000
#define GPA_TSS    0x5000
#define GPA_CODE   0x6000
#define GPA_STACK  0x8000            /* top of the ring-3 stack, grows down */
#define GPA_KERN   0x9000            /* what LSTAR points at */
#define MAPPED     0x00400000        /* four megabytes are backed */
#define GPA_HOLE   0x00800000        /* mapped in the page tables, not in the partition */

/* Entry points inside the code page, one per question. */
#define EP_RING3   (GPA_CODE + 0x000)
#define EP_SYSCALL (GPA_CODE + 0x100)
#define EP_FAULT   (GPA_CODE + 0x200)
#define EP_LOOP    (GPA_CODE + 0x300)

static int verbose = 0;

static void emit(const char *k, const char *fmt, ...)
{
	va_list ap;
	printf("%s=", k);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
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

static UINT8 *mem;                   /* the host allocation backing GPA 0 */

static void put64(UINT64 gpa, UINT64 v) { memcpy(mem + gpa, &v, 8); }

static void put_code(UINT64 gpa, const UINT8 *b, size_t n) { memcpy(mem + gpa, b, n); }

/* Identity page tables, one PML4 and one PDPT entry, eight two-megabyte pages.
 * Every level is user-accessible and writable; the guest never needs a
 * protection distinction the host does not already enforce. */
static void build_page_tables(void)
{
	int i;
	put64(GPA_PML4, GPA_PDPT | 0x7);
	put64(GPA_PDPT, GPA_PD | 0x7);
	for (i = 0; i < 8; i++)
		put64(GPA_PD + 8 * i, ((UINT64) i << 21) | 0x87);
}

/* Six descriptors plus a sixteen-byte TSS descriptor. The user selectors sit
 * where STAR wants them: syscall loads 0x08/0x10, sysret would load 0x2b/0x23. */
static void build_gdt(void)
{
	put64(GPA_GDT + 0x00, 0);
	put64(GPA_GDT + 0x08, 0x00af9b000000ffffULL);   /* ring-0 code, long */
	put64(GPA_GDT + 0x10, 0x00cf93000000ffffULL);   /* ring-0 data */
	put64(GPA_GDT + 0x18, 0x00cffb000000ffffULL);   /* ring-3 code, 32-bit; STAR base */
	put64(GPA_GDT + 0x20, 0x00cff3000000ffffULL);   /* ring-3 data, selector 0x23 */
	put64(GPA_GDT + 0x28, 0x00affb000000ffffULL);   /* ring-3 code, long, selector 0x2b */
	put64(GPA_GDT + 0x30, 0x0000890000000067ULL | ((UINT64)(GPA_TSS & 0xffffff) << 16)
	                      | ((UINT64)((GPA_TSS >> 24) & 0xff) << 56));
	put64(GPA_GDT + 0x38, 0);
	put64(GPA_TSS + 0x04, GPA_STACK);               /* rsp0, unused while no IDT exists */
}

/* Ring three cannot halt — hlt is privileged, and with no IDT a #GP there is a
 * triple fault rather than an exit — so the CPL-3 sequences leave through a
 * memory access the partition cannot satisfy, or through syscall. */
static const UINT8 code_ring3[] = {
	0x48, 0xc7, 0xc0, 0xee, 0xff, 0xc0, 0x00,       /* mov $0xc0ffee, %rax */
	0x48, 0xbb, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,  /* movabs $0x800000, %rbx */
	0x48, 0x8b, 0x13                                /* mov (%rbx), %rdx */
};

/* The syscall round trip, as a loop. One WHvRunVirtualProcessor call covers
 * syscall, the shim's hlt, sysret, and the jump back. */
static const UINT8 code_syscall[] = {
	0x0f, 0x05,                                     /* syscall */
	0xeb, 0xfc                                      /* jmp -4 */
};

static const UINT8 code_fault[] = {
	0x48, 0xbb, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,  /* movabs $0x800000, %rbx */
	0x48, 0x89, 0x03                                /* mov %rax, (%rbx) */
};

/* The cheapest exit, run at CPL 0. One increment so the host can prove
 * afterwards that the loop really went round, one hlt, one jump back. */
static const UINT8 code_loop[] = {
	0x48, 0xff, 0xc0,                               /* inc %rax */
	0xf4,                                           /* hlt */
	0xeb, 0xfa                                      /* jmp -6 */
};

/* The ring-0 side of the syscall path: LSTAR points here, it exits, and on the
 * next entry it returns to the caller. That is the whole mechanism section 4.3
 * rests on, reduced to four bytes. */
static const UINT8 code_kern[] = {
	0xf4,                                           /* hlt */
	0x48, 0x0f, 0x07                                /* sysretq */
};

static WHV_PARTITION_HANDLE part;

static HRESULT setreg(const WHV_REGISTER_NAME *n, const WHV_REGISTER_VALUE *v, UINT32 c)
{
	return WHvSetVirtualProcessorRegisters(part, 0, n, c, v);
}

static HRESULT getreg(const WHV_REGISTER_NAME *n, WHV_REGISTER_VALUE *v, UINT32 c)
{
	return WHvGetVirtualProcessorRegisters(part, 0, n, c, v);
}

static WHV_X64_SEGMENT_REGISTER seg(UINT16 sel, UINT8 type, int dpl, int lng, int def32)
{
	WHV_X64_SEGMENT_REGISTER s;
	memset(&s, 0, sizeof s);
	s.Base = 0;
	s.Limit = 0xffffffff;
	s.Selector = sel;
	s.SegmentType = type;
	s.NonSystemSegment = 1;
	s.DescriptorPrivilegeLevel = (UINT16) dpl;
	s.Present = 1;
	s.Long = (UINT16) lng;
	s.Default = (UINT16) def32;
	s.Granularity = 1;
	return s;
}

/* Long mode, ring three, one call. WHP takes the segment registers as cached
 * descriptors, so entering CPL 3 needs no iret: the selector's low two bits and
 * the descriptor's DPL are the privilege level, and the GDT below them only has
 * to agree. */
static HRESULT enter_long_mode(UINT64 rip, int cpl)
{
	WHV_REGISTER_NAME n[24];
	WHV_REGISTER_VALUE v[24];
	WHV_X64_SEGMENT_REGISTER data = cpl == 3 ? seg(0x23, 3, 3, 0, 1) : seg(0x10, 3, 0, 0, 1);
	WHV_X64_SEGMENT_REGISTER code = cpl == 3 ? seg(0x2b, 0xb, 3, 1, 0) : seg(0x08, 0xb, 0, 1, 0);
	int i = 0;

	memset(v, 0, sizeof v);

	n[i] = WHvX64RegisterCr0;    v[i++].Reg64 = 0x80010031ULL;   /* PE MP NE ET WP PG */
	n[i] = WHvX64RegisterCr3;    v[i++].Reg64 = GPA_PML4;
	n[i] = WHvX64RegisterCr4;    v[i++].Reg64 = 0x00000620ULL;   /* PAE OSFXSR OSXMMEXCPT */
	n[i] = WHvX64RegisterEfer;   v[i++].Reg64 = 0x00000d01ULL;   /* SCE LME LMA NXE */
	n[i] = WHvX64RegisterStar;   v[i++].Reg64 = (0x001bULL << 48) | (0x0008ULL << 32);
	n[i] = WHvX64RegisterLstar;  v[i++].Reg64 = GPA_KERN;
	n[i] = WHvX64RegisterSfmask; v[i++].Reg64 = 0x257fd5ULL;

	n[i] = WHvX64RegisterCs;     v[i++].Segment = code;
	n[i] = WHvX64RegisterSs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterDs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterEs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterFs;     v[i++].Segment = data;
	n[i] = WHvX64RegisterGs;     v[i++].Segment = data;

	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterTr;
	v[i].Segment.Base = GPA_TSS;
	v[i].Segment.Limit = 0x67;
	v[i].Segment.Selector = 0x30;
	v[i].Segment.SegmentType = 11;                 /* busy 64-bit TSS */
	v[i].Segment.Present = 1;
	i++;

	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterLdtr;
	i++;

	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterGdtr;
	v[i].Table.Base = GPA_GDT;
	v[i].Table.Limit = 0x3f;
	i++;

	/* No IDT. Nothing in the guest is meant to fault into one, and a triple
	 * fault reported as an unrecoverable exception is a better diagnostic
	 * than a handler that swallows the mistake. */
	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterIdtr;
	i++;

	n[i] = WHvX64RegisterRflags; v[i++].Reg64 = 0x2;
	n[i] = WHvX64RegisterRsp;    v[i++].Reg64 = GPA_STACK - 0x10;
	n[i] = WHvX64RegisterRip;    v[i++].Reg64 = rip;
	n[i] = WHvX64RegisterRax;    v[i++].Reg64 = 0;

	return setreg(n, v, (UINT32) i);
}

static int cmp_u64(const void *a, const void *b)
{
	UINT64 x = *(const UINT64 *) a, y = *(const UINT64 *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

/* Run until the vCPU stops, reporting the exit. Returns the exit reason, or
 * 0xffffffff when the run call itself failed. */
static UINT32 run_once(WHV_RUN_VP_EXIT_CONTEXT *ex)
{
	HRESULT hr = WHvRunVirtualProcessor(part, 0, ex, sizeof *ex);
	if (FAILED(hr)) {
		trace("WHvRunVirtualProcessor: 0x%08lx", (unsigned long) hr);
		return 0xffffffffu;
	}
	return ex->ExitReason;
}

static UINT64 reg64(WHV_REGISTER_NAME name)
{
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	if (FAILED(getreg(&name, &v, 1)))
		return 0;
	return v.Reg64;
}

static UINT16 cs_selector(void)
{
	WHV_REGISTER_NAME name = WHvX64RegisterCs;
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	if (FAILED(getreg(&name, &v, 1)))
		return 0xffff;
	return v.Segment.Selector;
}

/* The native comparison, in two parts, because the obvious call is not the
 * honest one. NtQuerySystemTime reads the shared user data page and never
 * leaves ring three on a modern build, so it prices a memory load; the second
 * measurement calls something that has to trap. Both are wanted: one bounds
 * what a Cygwin-style gate call costs, the other is the real ring transition a
 * WHP exit is being compared against.
 *
 * A per-call QueryPerformanceCounter reading is coarser than either call, so
 * time batches and take the median batch mean. */
static UINT64 native_call_ns(int trapping)
{
	typedef LONG (WINAPI *time_t_fn)(PLARGE_INTEGER);
	typedef LONG (WINAPI *info_fn)(HANDLE, UINT32, PVOID, UINT32, PUINT32);
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	time_t_fn qst = nt ? (time_t_fn) (void *) GetProcAddress(nt, "NtQuerySystemTime") : NULL;
	info_fn qip = nt ? (info_fn) (void *) GetProcAddress(nt, "NtQueryInformationProcess") : NULL;
	enum { batches = 200, per = 500 };
	UINT8 pbi[64];
	UINT32 got = 0;
	UINT64 *s;
	LARGE_INTEGER freq, a, b, t;
	UINT64 med;
	int i, j;

	if (trapping ? !qip : !qst)
		return 0;
	QueryPerformanceFrequency(&freq);
	s = malloc(batches * sizeof *s);
	if (!s)
		return 0;
	for (i = 0; i < batches; i++) {
		QueryPerformanceCounter(&a);
		for (j = 0; j < per; j++) {
			if (trapping)
				qip(GetCurrentProcess(), 0, pbi, sizeof pbi, &got);
			else
				qst(&t);
		}
		QueryPerformanceCounter(&b);
		s[i] = (UINT64) ((b.QuadPart - a.QuadPart) * 1000000000.0
		                 / (double) freq.QuadPart / per);
	}
	qsort(s, batches, sizeof *s, cmp_u64);
	med = s[batches / 2];
	free(s);
	return med;
}

/* Every question from `from` onward goes unanswered, and says so. A spike that
 * stops early has to print the same keys as one that does not, or the
 * transcript's shape depends on the answer. */
static void unreached(int from)
{
	if (from <= 2) emit("q2_partition_created", "%d", 0);
	if (from <= 3) emit("q3_memory_mapped", "%d", 0);
	if (from <= 4) emit("q4_vcpu_created", "%d", 0);
	if (from <= 5) emit("q5_long_mode_ring3", "%d", 0);
	if (from <= 6) emit("q6_syscall_exit", "%s", "unreached");
	if (from <= 7) emit("q7_page_fault_exit", "%d", 0);
	if (from <= 8) {
		emit("q8_exit_mechanism", "%s", "none");
		emit("q8_syscall_mechanism", "%s", "none");
		emit("q8_native_call_ns", "%llu", (unsigned long long) native_call_ns(0));
		emit("q8_native_syscall_ns", "%llu", (unsigned long long) native_call_ns(1));
	}
}

/* Time `iters` round trips through the loop already loaded, reporting the
 * median and the ninety-ninth percentile. Returns the number timed, which is
 * short of `iters` when an exit came back as something other than a halt. */
static UINT64 time_exits(UINT64 iters, UINT64 warm, const char *prefix)
{
	WHV_RUN_VP_EXIT_CONTEXT ex;
	LARGE_INTEGER freq, a, b;
	UINT64 *lat, good = 0, k, sum = 0;
	char key[64];

	QueryPerformanceFrequency(&freq);
	lat = malloc((size_t) iters * sizeof *lat);
	if (!lat)
		return 0;
	for (k = 0; k < warm; k++) {
		memset(&ex, 0, sizeof ex);
		if (run_once(&ex) != WHvRunVpExitReasonX64Halt) {
			trace("%s: warmup exit %u at %llu", prefix,
			      (unsigned) ex.ExitReason, (unsigned long long) k);
			free(lat);
			return 0;
		}
	}
	for (k = 0; k < iters; k++) {
		QueryPerformanceCounter(&a);
		memset(&ex, 0, sizeof ex);
		if (run_once(&ex) != WHvRunVpExitReasonX64Halt)
			break;
		QueryPerformanceCounter(&b);
		lat[good++] = (UINT64) ((b.QuadPart - a.QuadPart) * 1000000000.0
		                        / (double) freq.QuadPart);
	}
	if (good > 100) {
		for (k = 0; k < good; k++)
			sum += lat[k];
		qsort(lat, (size_t) good, sizeof *lat, cmp_u64);
		sprintf(key, "%s_median_ns", prefix); emit(key, "%llu", (unsigned long long) lat[good / 2]);
		sprintf(key, "%s_p99_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[(good * 99) / 100]);
		sprintf(key, "%s_min_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[0]);
		sprintf(key, "%s_mean_ns", prefix);   emit(key, "%llu", (unsigned long long) (sum / good));
	}
	sprintf(key, "%s_exits_timed", prefix);
	emit(key, "%llu", (unsigned long long) good);
	free(lat);
	return good;
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	WHV_PARTITION_PROPERTY prop;
	WHV_RUN_VP_EXIT_CONTEXT ex;
	HRESULT hr;
	UINT32 written = 0, reason;
	int present, i, q2, q3, q4, q5 = 0, q6 = 0, q7 = 0;
	UINT64 iters = 20000, warm = 2000;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc) {
			iters = strtoull(argv[++i], NULL, 0);
			warm = iters / 10 + 1;
			continue;
		}
		fprintf(stderr, "whp-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("windows hypervisor platform as a substrate\n\n\n");

	/* q1. No privilege needed, and the only question whose negative answer
	 * ends the spike rather than narrowing it. */
	memset(&cap, 0, sizeof cap);
	hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &written);
	present = SUCCEEDED(hr) && cap.HypervisorPresent;
	emit("q1_hypervisor_present", "%d", present);
	emit("q1_capability_hresult", "0x%08lx", (unsigned long) hr);
	if (present) {
		memset(&cap, 0, sizeof cap);
		if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeFeatures, &cap, sizeof cap, &written)))
			emit("q1_features", "0x%016llx", (unsigned long long) cap.Features.AsUINT64);
		memset(&cap, 0, sizeof cap);
		if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeProcessorVendor, &cap, sizeof cap, &written))) {
			UINT32 vend = (UINT32) cap.ProcessorVendor;
			emit("q1_processor_vendor", "%s",
			     vend == WHvProcessorVendorAmd ? "amd" :
			     vend == WHvProcessorVendorIntel ? "intel" :
			     vend == WHvProcessorVendorHygon ? "hygon" : "other");
		}
		memset(&cap, 0, sizeof cap);
		if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeProcessorFeatures, &cap, sizeof cap, &written)))
			emit("q1_processor_features", "0x%016llx",
			     (unsigned long long) cap.ProcessorFeatures.AsUINT64);
		memset(&cap, 0, sizeof cap);
		if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeExtendedVmExits, &cap, sizeof cap, &written)))
			emit("q1_extended_vm_exits", "0x%016llx",
			     (unsigned long long) cap.ExtendedVmExits.AsUINT64);
	} else {
		unreached(2);
		return 0;
	}

	/* q2. Create, size, set up. A failure here with the hypervisor present is
	 * the shape a disabled optional feature takes. */
	hr = WHvCreatePartition(&part);
	if (SUCCEEDED(hr)) {
		memset(&prop, 0, sizeof prop);
		prop.ProcessorCount = 1;
		hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeProcessorCount,
		                             &prop, sizeof prop);
	}
	if (SUCCEEDED(hr))
		hr = WHvSetupPartition(part);
	q2 = SUCCEEDED(hr);
	emit("q2_partition_created", "%d", q2);
	emit("q2_setup_hresult", "0x%08lx", (unsigned long) hr);
	if (!q2) {
		unreached(3);
		return 0;
	}

	/* q3. One host allocation is the guest's physical memory, which is the
	 * point of the substrate: the kernel reads user memory at the address the
	 * guest itself uses. */
	mem = VirtualAlloc(NULL, MAPPED, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	hr = mem ? WHvMapGpaRange(part, mem, 0, MAPPED,
	                          WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite
	                          | WHvMapGpaRangeFlagExecute)
	         : E_OUTOFMEMORY;
	q3 = SUCCEEDED(hr);
	emit("q3_memory_mapped", "%d", q3);
	emit("q3_map_hresult", "0x%08lx", (unsigned long) hr);
	emit("q3_mapped_bytes", "0x%x", (unsigned) MAPPED);
	emit("q3_unmapped_gpa", "0x%x", (unsigned) GPA_HOLE);
	if (!q3) {
		unreached(4);
		return 0;
	}

	memset(mem, 0, MAPPED);
	build_page_tables();
	build_gdt();
	put_code(EP_RING3, code_ring3, sizeof code_ring3);
	put_code(EP_SYSCALL, code_syscall, sizeof code_syscall);
	put_code(EP_FAULT, code_fault, sizeof code_fault);
	put_code(EP_LOOP, code_loop, sizeof code_loop);
	put_code(GPA_KERN, code_kern, sizeof code_kern);

	/* q4. */
	hr = WHvCreateVirtualProcessor(part, 0, 0);
	q4 = SUCCEEDED(hr);
	emit("q4_vcpu_created", "%d", q4);
	emit("q4_vcpu_hresult", "0x%08lx", (unsigned long) hr);
	if (q4) {
		hr = enter_long_mode(EP_RING3, 3);
		q4 = SUCCEEDED(hr);
		emit("q4_registers_set", "%d", q4);
		emit("q4_register_hresult", "0x%08lx", (unsigned long) hr);
	}
	if (!q4) {
		unreached(5);
		return 0;
	}

	/* q5. Three facts together say ring three ran: the guest got two
	 * instructions in and left on the third, the value it computed is in
	 * %rax, and CS is still the DPL-3 long-mode selector it was entered with. */
	memset(&ex, 0, sizeof ex);
	reason = run_once(&ex);
	emit("q5_exit_reason", "%u", reason);
	if (reason == WHvRunVpExitReasonMemoryAccess) {
		UINT64 rax = reg64(WHvX64RegisterRax);
		UINT16 cs = cs_selector();
		emit("q5_rax", "0x%llx", (unsigned long long) rax);
		emit("q5_cs_selector", "0x%04x", cs);
		emit("q5_cpl", "%u", (unsigned) (cs & 3));
		q5 = (rax == 0xc0ffeeULL) && ((cs & 3) == 3);
	}
	emit("q5_long_mode_ring3", "%d", q5);

	/* q6. The mechanism the substrate rests on. SCE is on and LSTAR points at
	 * the shim, so a ring-3 syscall arrives at the host as a halt inside the
	 * shim, with %rcx holding the user return address the instruction put
	 * there and CS switched to the ring-0 selector STAR names. */
	if (q5) {
		hr = enter_long_mode(EP_SYSCALL, 3);
		memset(&ex, 0, sizeof ex);
		reason = SUCCEEDED(hr) ? run_once(&ex) : 0xffffffffu;
		emit("q6_exit_reason", "%u", reason);
		if (reason == WHvRunVpExitReasonX64Halt) {
			UINT64 rip = reg64(WHvX64RegisterRip);
			UINT64 rcx = reg64(WHvX64RegisterRcx);
			UINT64 r11 = reg64(WHvX64RegisterR11);
			UINT16 cs = cs_selector();
			emit("q6_rip", "0x%llx", (unsigned long long) rip);
			emit("q6_rcx", "0x%llx", (unsigned long long) rcx);
			emit("q6_r11", "0x%llx", (unsigned long long) r11);
			emit("q6_cs_selector", "0x%04x", cs);
			q6 = (rip == GPA_KERN + 1) && (rcx == EP_SYSCALL + 2) && ((cs & 3) == 0);
		}
	}
	emit("q6_syscall_exit", "%s", q6 ? "halt-in-shim" : "no");

	/* q7. A page the guest's tables map and the partition does not, written
	 * rather than read, so the access type is in the answer too. */
	if (q5) {
		hr = enter_long_mode(EP_FAULT, 3);
		memset(&ex, 0, sizeof ex);
		reason = SUCCEEDED(hr) ? run_once(&ex) : 0xffffffffu;
		emit("q7_exit_reason", "%u", reason);
		if (reason == WHvRunVpExitReasonMemoryAccess) {
			emit("q7_gpa", "0x%llx", (unsigned long long) ex.MemoryAccess.Gpa);
			emit("q7_gva", "0x%llx", (unsigned long long) ex.MemoryAccess.Gva);
			emit("q7_access_type", "%u", (unsigned) ex.MemoryAccess.AccessInfo.AccessType);
			q7 = ex.MemoryAccess.Gpa == GPA_HOLE;
		}
	}
	emit("q7_page_fault_exit", "%d", q7);

	/* q8. Two numbers. The first is the cheapest exit this partition can be
	 * made to take repeatedly — a ring-0 hlt with a jump back to it, so one
	 * WHvRunVirtualProcessor call is one round trip and nothing else. The
	 * second is the one open question 1 asks for: a ring-3 syscall through the
	 * shim and back out through sysret, which is what a syscall under
	 * substrate H would actually cost before the kernel does any work. */
	emit("q8_exit_mechanism", "%s", "guest-hlt-cpl0");
	if (SUCCEEDED(enter_long_mode(EP_LOOP, 0))) {
		UINT64 good = time_exits(iters, warm, "q8_exit");
		if (good)
			emit("q8_loop_iterations", "0x%llx",
			     (unsigned long long) reg64(WHvX64RegisterRax));
	} else {
		emit("q8_exit_exits_timed", "%llu", 0ULL);
	}

	emit("q8_syscall_mechanism", "%s", q6 ? "ring3-syscall-hlt-sysret" : "none");
	if (q6 && SUCCEEDED(enter_long_mode(EP_SYSCALL, 3)))
		time_exits(iters, warm, "q8_syscall");
	else
		emit("q8_syscall_exits_timed", "%llu", 0ULL);

	emit("q8_native_call_ns", "%llu", (unsigned long long) native_call_ns(0));
	emit("q8_native_syscall_ns", "%llu", (unsigned long long) native_call_ns(1));

	WHvDeleteVirtualProcessor(part, 0);
	WHvUnmapGpaRange(part, 0, MAPPED);
	WHvDeletePartition(part);
	return 0;
}
