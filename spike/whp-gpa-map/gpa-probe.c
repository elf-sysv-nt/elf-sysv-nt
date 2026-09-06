/* gpa-probe: what WHvMapGpaRange does to the host memory behind it.
 *
 * Substrate H puts every Linux process's memory behind guest physical
 * addresses that WHvMapGpaRange ties to host virtual memory. Whether that call
 * pins the host pages, populates them, or leaves them lazy decides whether a
 * 64 GB MAP_NORESERVE under H is a reservation or a resident-RAM bill, and
 * whether the "lazy-commit handler one level down" 0011 § 4 promises can exist.
 * Nobody has measured it. This does.
 *
 * Seven questions, each a key=value block on stdout; measure.sh reads the keys
 * and states the verdict. Two of them (a reserve-only mapping touched by the
 * guest, memory decommitted or released behind a live mapping) can kill the
 * process that asks them, so they run in a child of this same binary and the
 * parent reports how the child died if it did.
 *
 * Built with Cygwin's gcc against w32api's winhvplatform, like spike/whp.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <winhvplatform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE "gpa-probe 1.1"

#define PAGE   0x1000ULL
#define MB     0x100000ULL
#define GB     0x40000000ULL

/* Guest physical layout. The control region is the first four megabytes;
 * the test regions sit at gigabyte boundaries so their addresses read
 * plainly in a transcript. Identity page tables cover the first four
 * gigabytes with two-megabyte pages. */
#define GPA_PML4   0x1000
#define GPA_PDPT   0x2000
#define GPA_PD0    0x3000            /* four PDs, one per gigabyte */
#define GPA_GDT    0x7000
#define GPA_TSS    0x8000
#define GPA_CODE   0x9000
#define GPA_STACK  0xb000
#define CONTROL    (4 * MB)

#define EP_READ    (GPA_CODE + 0x000)
#define EP_WRITE   (GPA_CODE + 0x100)

#define GPA_R1     (1 * GB)          /* committed host memory, one gigabyte */
#define GPA_R2     (2 * GB)          /* reserve-only host memory */
#define GPA_R3     (3 * GB)          /* unmapped at start; mapped on exit */
#define GPA_R4     (3 * GB + 512 * MB)  /* decommit and release test */
#define GPA_R5     (3 * GB + 768 * MB)  /* map-cost scratch */
#define GPA_BIG    (4 * GB)          /* q8, q9: the large mappings start here */

static int verbose;
static int big_gb = 64;            /* q8: the largest reserve-only mapping tried */
static int commit_gb = 8;          /* q9: the committed mapping the guest samples */
static int pressure_mb;            /* q9: host memory touched to make pressure; 0 skips */
static WHV_PARTITION_HANDLE part;
static UINT8 *ctl;

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

static void put64(UINT64 gpa, UINT64 v) { memcpy(ctl + gpa, &v, 8); }

/* The Is*Present helpers live in an API-set stub the import library lacks;
 * asking the DLL for the export by name answers the same question. */
static int has(const char *name)
{
	HMODULE m = GetModuleHandleA("WinHvPlatform.dll");
	return m && GetProcAddress(m, name) ? 1 : 0;
}
#define IsWHvAdviseGpaRangePresent()       has("WHvAdviseGpaRange")
#define IsWHvMapGpaRange2Present()         has("WHvMapGpaRange2")
#define IsWHvGetPartitionCountersPresent() has("WHvGetPartitionCounters")

static void build_tables(void)
{
	int pd, i;
	put64(GPA_PML4, GPA_PDPT | 0x7);
	for (pd = 0; pd < 4; pd++) {
		UINT64 pdbase = GPA_PD0 + (UINT64) pd * PAGE;
		put64(GPA_PDPT + 8 * pd, pdbase | 0x7);
		for (i = 0; i < 512; i++)
			put64(pdbase + 8 * i, (((UINT64) pd << 30) | ((UINT64) i << 21)) | 0x87);
	}
	/* gigabytes 4 to 511 as 1 GB pages (PDPTE.PS), for q8 and q9 */
	for (i = 4; i < 512; i++)
		put64(GPA_PDPT + 8 * i, ((UINT64) i << 30) | 0x87);
}

static void build_gdt(void)
{
	put64(GPA_GDT + 0x00, 0);
	put64(GPA_GDT + 0x08, 0x00af9b000000ffffULL);   /* ring-0 code, long */
	put64(GPA_GDT + 0x10, 0x00cf93000000ffffULL);   /* ring-0 data */
	put64(GPA_GDT + 0x30, 0x0000890000000067ULL | ((UINT64)(GPA_TSS & 0xffffff) << 16)
	                      | ((UINT64)((GPA_TSS >> 24) & 0xff) << 56));
	put64(GPA_TSS + 0x04, GPA_STACK);
}

/* mov (%rbx),%rax ; hlt ; jmp back. And the store twin. Ring 0, so hlt is
 * the exit and nothing else in the loop can fault unless the address does. */
static const UINT8 code_read[]  = { 0x48, 0x8b, 0x03, 0xf4, 0xeb, 0xfa };
static const UINT8 code_write[] = { 0x48, 0x89, 0x03, 0xf4, 0xeb, 0xfa };

static WHV_X64_SEGMENT_REGISTER seg(UINT16 sel, UINT8 type, int lng, int def32)
{
	WHV_X64_SEGMENT_REGISTER s;
	memset(&s, 0, sizeof s);
	s.Limit = 0xffffffff;
	s.Selector = sel;
	s.SegmentType = type;
	s.NonSystemSegment = 1;
	s.Present = 1;
	s.Long = (UINT16) lng;
	s.Default = (UINT16) def32;
	s.Granularity = 1;
	return s;
}

static HRESULT enter(UINT64 rip, UINT64 rbx, UINT64 rax)
{
	WHV_REGISTER_NAME n[24];
	WHV_REGISTER_VALUE v[24];
	WHV_X64_SEGMENT_REGISTER data = seg(0x10, 3, 0, 1);
	WHV_X64_SEGMENT_REGISTER code = seg(0x08, 0xb, 1, 0);
	int i = 0;

	memset(v, 0, sizeof v);
	n[i] = WHvX64RegisterCr0;    v[i++].Reg64 = 0x80010031ULL;
	n[i] = WHvX64RegisterCr3;    v[i++].Reg64 = GPA_PML4;
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

static HRESULT set_rbx_rax(UINT64 rbx, UINT64 rax)
{
	WHV_REGISTER_NAME n[2] = { WHvX64RegisterRbx, WHvX64RegisterRax };
	WHV_REGISTER_VALUE v[2];
	memset(v, 0, sizeof v);
	v[0].Reg64 = rbx;
	v[1].Reg64 = rax;
	return WHvSetVirtualProcessorRegisters(part, 0, n, 2, v);
}

static UINT64 get_rax(void)
{
	WHV_REGISTER_NAME n = WHvX64RegisterRax;
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	if (FAILED(WHvGetVirtualProcessorRegisters(part, 0, &n, 1, &v)))
		return ~0ULL;
	return v.Reg64;
}

static UINT32 run(WHV_RUN_VP_EXIT_CONTEXT *ex)
{
	HRESULT hr;
	memset(ex, 0, sizeof *ex);
	hr = WHvRunVirtualProcessor(part, 0, ex, sizeof *ex);
	if (FAILED(hr)) {
		trace("run: 0x%08lx", (unsigned long) hr);
		return 0xffffffffu;
	}
	return ex->ExitReason;
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
	if (n == 0) {
		sprintf(key, "%s_samples", prefix); emit(key, "0");
		return;
	}
	qsort(lat, n, sizeof *lat, cmp_u64);
	sprintf(key, "%s_median_ns", prefix); emit(key, "%llu", (unsigned long long) lat[n / 2]);
	sprintf(key, "%s_p99_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[(n * 99) / 100]);
	sprintf(key, "%s_min_ns", prefix);    emit(key, "%llu", (unsigned long long) lat[0]);
	sprintf(key, "%s_samples", prefix);   emit(key, "%llu", (unsigned long long) n);
}

/* Host-side view of a range: how many of `samples` evenly spaced pages are
 * resident, and how many of those the memory manager reports locked. This is
 * the pinning question asked of the page tables rather than of the API. */
static void residency(const char *prefix, UINT8 *base, UINT64 size, int samples)
{
	PSAPI_WORKING_SET_EX_INFORMATION *wsi;
	int i, valid = 0, locked = 0, shared = 0;
	char key[80];

	wsi = calloc((size_t) samples, sizeof *wsi);
	if (!wsi)
		return;
	for (i = 0; i < samples; i++)
		wsi[i].VirtualAddress = base + (size / (UINT64) samples) * (UINT64) i;
	if (QueryWorkingSetEx(GetCurrentProcess(), wsi, (DWORD) (samples * sizeof *wsi))) {
		for (i = 0; i < samples; i++) {
			if (wsi[i].VirtualAttributes.Valid) {
				valid++;
				if (wsi[i].VirtualAttributes.Locked)
					locked++;
				if (wsi[i].VirtualAttributes.Shared)
					shared++;
			}
		}
	}
	sprintf(key, "%s_sampled", prefix);  emit(key, "%d", samples);
	sprintf(key, "%s_resident", prefix); emit(key, "%d", valid);
	sprintf(key, "%s_locked", prefix);   emit(key, "%d", locked);
	sprintf(key, "%s_shared", prefix);   emit(key, "%d", shared);
	free(wsi);
}

static UINT64 working_set(void)
{
	PROCESS_MEMORY_COUNTERS pmc;
	memset(&pmc, 0, sizeof pmc);
	pmc.cb = sizeof pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
	return pmc.WorkingSetSize;
}

/* Partition, control memory, one vCPU. Returns 0 when any of it fails. */
static int bring_up(void)
{
	WHV_PARTITION_PROPERTY prop;
	HRESULT hr;

	hr = WHvCreatePartition(&part);
	if (FAILED(hr)) { emit("setup_create_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	memset(&prop, 0, sizeof prop);
	prop.ProcessorCount = 1;
	hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof prop);
	if (SUCCEEDED(hr))
		hr = WHvSetupPartition(part);
	if (FAILED(hr)) { emit("setup_setup_hresult", "0x%08lx", (unsigned long) hr); return 0; }

	ctl = VirtualAlloc(NULL, CONTROL, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!ctl) return 0;
	memset(ctl, 0, CONTROL);
	build_tables();
	build_gdt();
	memcpy(ctl + EP_READ, code_read, sizeof code_read);
	memcpy(ctl + EP_WRITE, code_write, sizeof code_write);
	hr = WHvMapGpaRange(part, ctl, 0, CONTROL,
	                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
	if (FAILED(hr)) { emit("setup_map_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	hr = WHvCreateVirtualProcessor(part, 0, 0);
	if (FAILED(hr)) { emit("setup_vcpu_hresult", "0x%08lx", (unsigned long) hr); return 0; }
	return 1;
}

/* One guest load from `gpa`. Returns the exit reason; *val is %rax after a
 * halt. The first call enters at EP_READ; later ones re-aim %rbx and resume
 * through the loop's jmp. */
static WHV_RUN_VP_EXIT_CONTEXT last_ex;

static const char *reason_word(UINT32 r)
{
	switch (r) {
	case WHvRunVpExitReasonX64Halt: return "halt";
	case WHvRunVpExitReasonMemoryAccess: return "memory-access-exit";
	case WHvRunVpExitReasonUnrecoverableException: return "unrecoverable-exception";
	case WHvRunVpExitReasonInvalidVpRegisterValue: return "invalid-register";
	case 0xffffffffu: return "run-call-failed";
	default: return "other-exit";
	}
}

static UINT32 guest_read(UINT64 gpa, UINT64 *val, int first)
{
	UINT32 r;
	if (first) enter(EP_READ, gpa, 0); else set_rbx_rax(gpa, 0);
	r = run(&last_ex);
	*val = r == WHvRunVpExitReasonX64Halt ? get_rax() : ~0ULL;
	return r;
}

/* For a memory-access exit: whether the hypervisor says the GPA was unmapped
 * (no WHvMapGpaRange covers it) or mapped and unbackable (the host page is
 * absent). The kernel under H has to tell these apart, so the transcript does. */
static void emit_access(const char *key)
{
	if (last_ex.ExitReason != WHvRunVpExitReasonMemoryAccess) { emit(key, "%s", reason_word(last_ex.ExitReason)); return; }
	emit(key, "%s,%s,gpa:0x%llx",
	     last_ex.MemoryAccess.AccessInfo.GpaUnmapped ? "gpa-unmapped" : "gpa-mapped-host-absent",
	     last_ex.MemoryAccess.AccessInfo.AccessType == WHvMemoryAccessWrite ? "write" : "read",
	     (unsigned long long) last_ex.MemoryAccess.Gpa);
}

static void hv_counters(const char *prefix)
{
	WHV_PARTITION_MEMORY_COUNTERS mc;
	UINT32 got = 0;
	char key[80];
	if (!IsWHvGetPartitionCountersPresent()) return;
	memset(&mc, 0, sizeof mc);
	if (FAILED(WHvGetPartitionCounters(part, WHvPartitionCounterSetMemory, &mc, sizeof mc, &got))) return;
	sprintf(key, "%s_hv_mapped_4k_pages", prefix); emit(key, "%llu", (unsigned long long) mc.Mapped4KPageCount);
	sprintf(key, "%s_hv_mapped_2m_pages", prefix); emit(key, "%llu", (unsigned long long) mc.Mapped2MPageCount);
	sprintf(key, "%s_hv_mapped_1g_pages", prefix); emit(key, "%llu", (unsigned long long) mc.Mapped1GPageCount);
}

static UINT32 guest_write(UINT64 gpa, UINT64 val, int first)
{
	WHV_RUN_VP_EXIT_CONTEXT ex;
	if (first) enter(EP_WRITE, gpa, val); else set_rbx_rax(gpa, val);
	return run(&ex);
}

/* ---- q1: what the API admits to -------------------------------------- */

static void q1_capabilities(void)
{
	WHV_CAPABILITY cap;
	UINT32 w = 0;
	memset(&cap, 0, sizeof cap);
	emit("q1_advise_gpa_range_present", "%d", IsWHvAdviseGpaRangePresent() ? 1 : 0);
	emit("q1_map_gpa_range2_present", "%d", IsWHvMapGpaRange2Present() ? 1 : 0);
	emit("q1_partition_counters_present", "%d", IsWHvGetPartitionCountersPresent() ? 1 : 0);
	if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeGpaRangePopulateFlags, &cap, sizeof cap, &w)))
		emit("q1_populate_flags", "0x%08x", (unsigned) cap.GpaRangePopulateFlags.AsUINT32);
	else
		emit("q1_populate_flags", "unsupported");
	memset(&cap, 0, sizeof cap);
	if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodePhysicalAddressWidth, &cap, sizeof cap, &w)))
		emit("q1_physical_address_width", "%u", (unsigned) cap.PhysicalAddressWidth);
}

/* ---- q2: does mapping populate or pin? ------------------------------- */

static UINT8 *r1;

static void q2_map_committed(void)
{
	UINT64 ws0, ws1, t0, t1;
	HRESULT hr;

	r1 = VirtualAlloc(NULL, GB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	emit("q2_commit_1gb", "%d", r1 ? 1 : 0);
	if (!r1) return;
	residency("q2_before_map", r1, GB, 256);
	ws0 = working_set();
	t0 = now_ns();
	hr = WHvMapGpaRange(part, r1, GPA_R1, GB,
	                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
	t1 = now_ns();
	ws1 = working_set();
	emit("q2_map_hresult", "0x%08lx", (unsigned long) hr);
	emit("q2_map_1gb_ns", "%llu", (unsigned long long) (t1 - t0));
	emit("q2_working_set_before_kb", "%llu", (unsigned long long) (ws0 / 1024));
	emit("q2_working_set_after_kb", "%llu", (unsigned long long) (ws1 / 1024));
	emit("q2_working_set_delta_kb", "%lld", (long long) ((INT64) ws1 - (INT64) ws0) / 1024);
	residency("q2_after_map", r1, GB, 256);
	if (IsWHvGetPartitionCountersPresent()) {
		WHV_PARTITION_MEMORY_COUNTERS mc;
		UINT32 got = 0;
		memset(&mc, 0, sizeof mc);
		if (SUCCEEDED(WHvGetPartitionCounters(part, WHvPartitionCounterSetMemory, &mc, sizeof mc, &got))) {
			emit("q2_hv_mapped_4k_pages", "%llu", (unsigned long long) mc.Mapped4KPageCount);
			emit("q2_hv_mapped_2m_pages", "%llu", (unsigned long long) mc.Mapped2MPageCount);
			emit("q2_hv_mapped_1g_pages", "%llu", (unsigned long long) mc.Mapped1GPageCount);
		}
	}
}

/* ---- q3: the guest touches mapped, untouched pages ------------------- */

static void q3_guest_touch(void)
{
	enum { n = 256 };
	UINT64 lat_first[n], lat_again[n], val, t0;
	UINT32 r;
	int i, ok = 0, bad = 0;

	if (!r1) return;
	/* first touch of each sampled page, from the guest */
	for (i = 0; i < n; i++) {
		UINT64 gpa = GPA_R1 + (GB / n) * (UINT64) i;
		t0 = now_ns();
		r = guest_read(gpa, &val, i == 0);
		lat_first[i] = now_ns() - t0;
		if (r == WHvRunVpExitReasonX64Halt && val == 0) ok++; else bad++;
	}
	emit("q3_first_touch_reads_zero", "%d", ok);
	emit("q3_first_touch_other", "%d", bad);
	stats("q3_first_touch", lat_first, n);
	for (i = 0; i < n; i++) {
		UINT64 gpa = GPA_R1 + (GB / n) * (UINT64) i;
		t0 = now_ns();
		guest_read(gpa, &val, 0);
		lat_again[i] = now_ns() - t0;
	}
	stats("q3_second_touch", lat_again, n);
	residency("q3_after_touch", r1, GB, 256);
	emit("q3_working_set_kb", "%llu", (unsigned long long) (working_set() / 1024));

	/* and a guest write, read back by the host: the same page both ways */
	r = guest_write(GPA_R1 + 8 * PAGE, 0x6775657374ULL, 1);
	emit("q3_guest_write_exit", "%s", reason_word(r));
	emit("q3_host_sees_guest_write", "%d", *(UINT64 *) (r1 + 8 * PAGE) == 0x6775657374ULL);
}

/* ---- q4 (child): reserve-only host memory behind a mapping ----------- */

static void q4_reserve_only(void)
{
	UINT8 *r2 = VirtualAlloc(NULL, GB, MEM_RESERVE, PAGE_READWRITE);
	HRESULT hr;
	UINT64 val;
	UINT32 r;

	emit("q4_reserve_1gb", "%d", r2 ? 1 : 0);
	if (!r2) return;
	hr = WHvMapGpaRange(part, r2, GPA_R2, GB, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
	emit("q4_map_reserved_hresult", "0x%08lx", (unsigned long) hr);
	if (FAILED(hr)) {
		emit("q4_map_reserved", "refused");
		return;
	}
	emit("q4_map_reserved", "accepted");
	r = guest_read(GPA_R2 + 16 * PAGE, &val, 1);
	emit("q4_guest_touch_reserved_exit", "%s", reason_word(r));
	emit_access("q4_guest_touch_reserved_access");
	emit("q4_guest_touch_reserved_value", "0x%llx", (unsigned long long) val);
	/* commit the page behind it now, and touch again */
	emit("q4_commit_behind", "%d", VirtualAlloc(r2 + 16 * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE) ? 1 : 0);
	r = guest_read(GPA_R2 + 16 * PAGE, &val, 0);
	emit("q4_guest_touch_after_commit_exit", "%s", reason_word(r));
	emit("q4_guest_touch_after_commit_value", "0x%llx", (unsigned long long) val);

	/* The lazy-commit path under H, timed: a reserved page behind a live
	 * mapping, touched by the guest, committed by the host on the exit, and
	 * the touch resumed. Compare spike 37's q7, the same thing under N. */
	{
		enum { n = 256 };
		UINT64 lat[n], t0;
		int i, exits = 0, resumed = 0;
		for (i = 0; i < n; i++) {
			UINT64 gpa = GPA_R2 + 64 * PAGE + (UINT64) i * PAGE;
			t0 = now_ns();
			r = guest_read(gpa, &val, 0);
			if (r == WHvRunVpExitReasonMemoryAccess) {
				exits++;
				VirtualAlloc(r2 + 64 * PAGE + (UINT64) i * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE);
				r = guest_read(gpa, &val, 0);   /* re-aims rbx; the loop re-touches */
				if (r == WHvRunVpExitReasonX64Halt && val == 0) resumed++;
			}
			lat[i] = now_ns() - t0;
		}
		emit("q4_commit_on_exit_exits", "%d", exits);
		emit("q4_commit_on_exit_resumed", "%d", resumed);
		stats("q4_commit_on_exit", lat, n);
	}
}

/* ---- q5 (child): decommit and release behind a live mapping ---------- */

static void q5_decommit_release(void)
{
	UINT8 *r4 = VirtualAlloc(NULL, 64 * MB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	HRESULT hr;
	UINT64 val;
	UINT32 r;
	BOOL ok;

	if (!r4) { emit("q5_commit_64mb", "0"); return; }
	emit("q5_commit_64mb", "1");
	hr = WHvMapGpaRange(part, r4, GPA_R4, 64 * MB, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
	emit("q5_map_hresult", "0x%08lx", (unsigned long) hr);
	if (FAILED(hr)) return;

	*(UINT64 *) (r4 + 5 * PAGE) = 0x1111222233334444ULL;
	r = guest_read(GPA_R4 + 5 * PAGE, &val, 1);
	emit("q5_guest_reads_host_write", "%d", r == WHvRunVpExitReasonX64Halt && val == 0x1111222233334444ULL);

	ok = VirtualFree(r4 + 5 * PAGE, PAGE, MEM_DECOMMIT);
	emit("q5_decommit_behind_map", "%s", ok ? "succeeded" : "refused");
	emit("q5_decommit_lasterror", "%lu", (unsigned long) (ok ? 0 : GetLastError()));
	r = guest_read(GPA_R4 + 5 * PAGE, &val, 0);
	emit("q5_guest_touch_decommitted_exit", "%s", reason_word(r));
	emit_access("q5_guest_touch_decommitted_access");
	emit("q5_guest_touch_decommitted_value", "0x%llx", (unsigned long long) val);

	ok = VirtualAlloc(r4 + 5 * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE) != NULL;
	emit("q5_recommit", "%d", ok);
	r = guest_read(GPA_R4 + 5 * PAGE, &val, 0);
	emit("q5_guest_touch_recommitted_exit", "%s", reason_word(r));
	emit("q5_guest_touch_recommitted_value", "0x%llx", (unsigned long long) val);

	/* the harder case: the whole allocation released while still mapped */
	ok = VirtualFree(r4, 0, MEM_RELEASE);
	emit("q5_release_behind_map", "%s", ok ? "succeeded" : "refused");
	emit("q5_release_lasterror", "%lu", (unsigned long) (ok ? 0 : GetLastError()));
	if (ok) {
		r = guest_read(GPA_R4 + 6 * PAGE, &val, 0);
		emit("q5_guest_touch_released_exit", "%s", reason_word(r));
		emit_access("q5_guest_touch_released_access");
		emit("q5_guest_touch_released_value", "0x%llx", (unsigned long long) val);
	}
}

/* ---- q6: what a map call costs --------------------------------------- */

static void q6_map_cost(void)
{
	enum { n4k = 2048, n2m = 32 };
	UINT8 *pool4 = VirtualAlloc(NULL, n4k * PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	UINT8 *pool2 = VirtualAlloc(NULL, n2m * 2 * MB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	UINT64 *lat = malloc(n4k * sizeof *lat);
	UINT64 t0;
	HRESULT hr;
	int i, fail = 0;

	if (!pool4 || !pool2 || !lat) { emit("q6_pools", "0"); return; }
	emit("q6_pools", "1");
	memset(pool4, 1, n4k * PAGE);       /* resident, so the map is not paying a fault */
	memset(pool2, 1, n2m * 2 * MB);

	for (i = 0; i < n4k; i++) {
		t0 = now_ns();
		hr = WHvMapGpaRange(part, pool4 + (UINT64) i * PAGE, GPA_R5 + (UINT64) i * PAGE, PAGE,
		                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
		lat[i] = now_ns() - t0;
		if (FAILED(hr)) fail++;
	}
	emit("q6_map_4k_failures", "%d", fail);
	stats("q6_map_4k", lat, n4k);
	for (i = 0; i < n4k; i++) {
		t0 = now_ns();
		WHvUnmapGpaRange(part, GPA_R5 + (UINT64) i * PAGE, PAGE);
		lat[i] = now_ns() - t0;
	}
	stats("q6_unmap_4k", lat, n4k);

	fail = 0;
	for (i = 0; i < n2m; i++) {
		t0 = now_ns();
		hr = WHvMapGpaRange(part, pool2 + (UINT64) i * 2 * MB, GPA_R5 + (UINT64) i * 2 * MB, 2 * MB,
		                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
		lat[i] = now_ns() - t0;
		if (FAILED(hr)) fail++;
	}
	emit("q6_map_2m_failures", "%d", fail);
	stats("q6_map_2m", lat, n2m);
	for (i = 0; i < n2m; i++) {
		t0 = now_ns();
		WHvUnmapGpaRange(part, GPA_R5 + (UINT64) i * 2 * MB, 2 * MB);
		lat[i] = now_ns() - t0;
	}
	stats("q6_unmap_2m", lat, n2m);

	/* Populate advice on 64 MB of the committed gigabyte: does the API's own
	 * prefetch make pages resident, and what does it cost? Context only. */
	if (IsWHvAdviseGpaRangePresent() && r1) {
		WHV_MEMORY_RANGE_ENTRY range = { GPA_R1 + 512 * MB, 64 * MB };
		WHV_ADVISE_GPA_RANGE adv;
		memset(&adv, 0, sizeof adv);
		adv.Populate.Flags.Prefetch = 1;
		adv.Populate.AccessType = WHvMemoryAccessWrite;
		t0 = now_ns();
		hr = WHvAdviseGpaRange(part, &range, 1, WHvAdviseGpaRangeCodePopulate, &adv, sizeof adv);
		emit("q6_populate_64mb_hresult", "0x%08lx", (unsigned long) hr);
		emit("q6_populate_64mb_ns", "%llu", (unsigned long long) (now_ns() - t0));
		residency("q6_after_populate", r1 + 512 * MB, 64 * MB, 64);
		{
			/* a guest first touch inside the populated range: does the
			 * populate advice buy back the demand-fault cost q3 measured? */
			UINT64 lat[64], val, t0;
			int k;
			for (k = 0; k < 64; k++) {
				t0 = now_ns();
				guest_read(GPA_R1 + 512 * MB + (UINT64) k * MB + PAGE, &val, k == 0);
				lat[k] = now_ns() - t0;
			}
			stats("q6_first_touch_after_populate", lat, 64);
		}
		memset(&adv, 0, sizeof adv);
		t0 = now_ns();
		hr = WHvAdviseGpaRange(part, &range, 1, WHvAdviseGpaRangeCodePin, NULL, 0);
		emit("q6_pin_64mb_hresult", "0x%08lx", (unsigned long) hr);
		emit("q6_pin_64mb_ns", "%llu", (unsigned long long) (now_ns() - t0));
		residency("q6_after_pin", r1 + 512 * MB, 64 * MB, 64);
		hr = WHvAdviseGpaRange(part, &range, 1, WHvAdviseGpaRangeCodeUnpin, NULL, 0);
		emit("q6_unpin_64mb_hresult", "0x%08lx", (unsigned long) hr);
	}
	VirtualFree(pool4, 0, MEM_RELEASE);
	VirtualFree(pool2, 0, MEM_RELEASE);
	free(lat);
}

/* ---- q7: mapping on demand from a memory-access exit ----------------- */

static void q7_lazy_map_on_exit(void)
{
	enum { n = 1024 };
	UINT8 *pool = VirtualAlloc(NULL, n * PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	UINT64 *lat = malloc(n * sizeof *lat);
	WHV_RUN_VP_EXIT_CONTEXT ex;
	UINT64 t0;
	UINT32 r;
	int i, unmapped_exits = 0, resumed = 0, wrong = 0;

	if (!pool || !lat) { emit("q7_pool", "0"); return; }
	emit("q7_pool", "1");
	memset(pool, 0, n * PAGE);
	for (i = 0; i < n; i++) {
		UINT64 gpa = GPA_R3 + (UINT64) i * PAGE;
		*(UINT64 *) (pool + (UINT64) i * PAGE) = 0xfeed0000ULL + (UINT64) i;
		t0 = now_ns();
		if (i == 0) enter(EP_READ, gpa, 0); else set_rbx_rax(gpa, 0);
		r = run(&ex);
		if (r == WHvRunVpExitReasonMemoryAccess && ex.MemoryAccess.AccessInfo.GpaUnmapped
		    && (ex.MemoryAccess.Gpa & ~(PAGE - 1)) == gpa) {
			unmapped_exits++;
			WHvMapGpaRange(part, pool + (UINT64) i * PAGE, gpa, PAGE,
			               WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			r = run(&ex);
			if (r == WHvRunVpExitReasonX64Halt) {
				resumed++;
				if (get_rax() != 0xfeed0000ULL + (UINT64) i) wrong++;
			}
		}
		lat[i] = now_ns() - t0;
	}
	emit("q7_unmapped_exits", "%d", unmapped_exits);
	emit("q7_resumed_after_map", "%d", resumed);
	emit("q7_wrong_values", "%d", wrong);
	stats("q7_fault_map_resume", lat, n);
	VirtualFree(pool, 0, MEM_RELEASE);
}

/* ---- q8 (child): reserve-only mappings at tens of gigabytes ---------- */

/* Spike 42's q2 and q4 at one gigabyte, scaled: a reserve-only host range
 * of 8, 16, 32 and 64 GB mapped in one call. What the call costs, whether
 * the working set moves, what the hypervisor counts, and whether the top
 * page of each mapping is reachable from the guest once committed. */
/* A map call at this size has been seen to fail once with
 * ERROR_NO_SYSTEM_RESOURCES (0x800705aa) and succeed on a retry moments
 * later; the transcript counts the retries so the kernel knows to make them. */
static HRESULT map_retrying(void *host, UINT64 gpa, UINT64 len, int *retries)
{
	HRESULT hr;
	int i;
	*retries = 0;
	for (i = 0; i < 6; i++) {
		hr = WHvMapGpaRange(part, host, gpa, len, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
		if (hr != (HRESULT) 0x800705aa) return hr;
		(*retries)++;
		Sleep(250);
	}
	return hr;
}

static void q8_reserve_at_scale(void)
{
	static const int sizes[] = { 8, 16, 32, 64 };
	unsigned k;
	int first = 1;

	for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
		int gb = sizes[k];
		UINT64 len = (UINT64) gb * GB, ws0, ws1, t0, t1, val;
		UINT8 *p;
		char key[64], pfx[32];
		HRESULT hr;
		UINT32 r;
		int retries;

		if (gb > big_gb) break;
		sprintf(pfx, "q8_%dgb", gb);
		p = VirtualAlloc(NULL, len, MEM_RESERVE, PAGE_READWRITE);
		sprintf(key, "%s_reserve", pfx);
		emit(key, "%d", p ? 1 : 0);
		if (!p) { sprintf(key, "%s_reserve_lasterror", pfx); emit(key, "%lu", (unsigned long) GetLastError()); continue; }
		ws0 = working_set();
		t0 = now_ns();
		hr = map_retrying(p, GPA_BIG, len, &retries);
		t1 = now_ns();
		ws1 = working_set();
		sprintf(key, "%s_map_hresult", pfx); emit(key, "0x%08lx", (unsigned long) hr);
		sprintf(key, "%s_map_retries", pfx); emit(key, "%d", retries);
		sprintf(key, "%s_map_ns", pfx); emit(key, "%llu", (unsigned long long) (t1 - t0));
		sprintf(key, "%s_map_ns_per_gb", pfx); emit(key, "%llu", (unsigned long long) ((t1 - t0) / (UINT64) gb));
		sprintf(key, "%s_working_set_delta_kb", pfx); emit(key, "%lld", (long long) ((INT64) ws1 - (INT64) ws0) / 1024);
		if (SUCCEEDED(hr)) {
			UINT64 top = GPA_BIG + len - PAGE;
			hv_counters(pfx);
			/* the guest touches the top page: reserve-only, so an exit */
			r = guest_read(top, &val, first);
			first = 0;
			sprintf(key, "%s_top_touch_reserved", pfx); emit_access(key);
			/* commit that one page and the touch completes */
			if (VirtualAlloc(p + len - PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE)) {
				*(UINT64 *) (p + len - PAGE) = 0x746f70ULL ^ top;
				r = guest_read(top, &val, 0);
				sprintf(key, "%s_top_touch_committed", pfx);
				emit(key, "%s,value-correct:%d", reason_word(r), r == WHvRunVpExitReasonX64Halt && val == (0x746f70ULL ^ top));
			}
			t0 = now_ns();
			hr = WHvUnmapGpaRange(part, GPA_BIG, len);
			t1 = now_ns();
			sprintf(key, "%s_unmap_hresult", pfx); emit(key, "0x%08lx", (unsigned long) hr);
			sprintf(key, "%s_unmap_ns", pfx); emit(key, "%llu", (unsigned long long) (t1 - t0));
		}
		VirtualFree(p, 0, MEM_RELEASE);
	}
}

/* ---- q9 (child): a committed mapping at scale, touched, then trimmed -- */

/* Spike 42's q2 and q3 at eight gigabytes, then the question they left:
 * what happens to guest-touched pages when the host takes the working set
 * away. EmptyWorkingSet is the trim a memory-pressured host performs,
 * applied on demand; --pressure-mb adds real pressure from a host
 * allocation touched page by page. After each, the guest reads its pages
 * back and the transcript says whether the values survived and what the
 * re-touch cost. */
static void q9_committed_at_scale(void)
{
	enum { n = 1024 };
	UINT64 len = (UINT64) commit_gb * GB, ws0, ws1, t0, t1, val;
	UINT64 *lat = malloc(n * sizeof *lat);
	UINT8 *p;
	HRESULT hr;
	UINT32 r;
	int i, ok = 0, bad = 0, retries;

	p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	emit("q9_commit", "%d", p ? 1 : 0);
	emit("q9_commit_gb", "%d", commit_gb);
	if (!p || !lat) { emit("q9_commit_lasterror", "%lu", (unsigned long) GetLastError()); return; }
	residency("q9_before_map", p, len, n);
	ws0 = working_set();
	t0 = now_ns();
	hr = map_retrying(p, GPA_BIG, len, &retries);
	t1 = now_ns();
	ws1 = working_set();
	emit("q9_map_hresult", "0x%08lx", (unsigned long) hr);
	emit("q9_map_retries", "%d", retries);
	emit("q9_map_ns", "%llu", (unsigned long long) (t1 - t0));
	emit("q9_map_working_set_delta_kb", "%lld", (long long) ((INT64) ws1 - (INT64) ws0) / 1024);
	residency("q9_after_map", p, len, n);
	hv_counters("q9");
	if (FAILED(hr)) return;

	/* the guest writes a tag into n pages spread over the range */
	for (i = 0; i < n; i++) {
		UINT64 gpa = GPA_BIG + (len / n) * (UINT64) i;
		t0 = now_ns();
		r = guest_write(gpa, 0x746167ULL ^ gpa, i == 0);
		lat[i] = now_ns() - t0;
		if (r == WHvRunVpExitReasonX64Halt) ok++; else bad++;
	}
	emit("q9_guest_first_touch_halted", "%d", ok);
	emit("q9_guest_first_touch_other", "%d", bad);
	stats("q9_guest_first_touch", lat, n);
	residency("q9_after_touch", p, len, n);
	emit("q9_after_touch_working_set_delta_kb", "%lld", (long long) ((INT64) working_set() - (INT64) ws0) / 1024);

	/* the trim: the host takes the working set away */
	emit("q9_empty_working_set", "%d", EmptyWorkingSet(GetCurrentProcess()) ? 1 : 0);
	residency("q9_after_trim", p, len, n);
	emit("q9_after_trim_working_set_kb", "%llu", (unsigned long long) (working_set() / 1024));
	ok = bad = 0;
	for (i = 0; i < n; i++) {
		UINT64 gpa = GPA_BIG + (len / n) * (UINT64) i;
		t0 = now_ns();
		r = guest_read(gpa, &val, i == 0);
		lat[i] = now_ns() - t0;
		if (r == WHvRunVpExitReasonX64Halt && val == (0x746167ULL ^ gpa)) ok++; else bad++;
	}
	emit("q9_after_trim_guest_reads_correct", "%d", ok);
	emit("q9_after_trim_guest_reads_other", "%d", bad);
	if (bad) emit_access("q9_after_trim_last_exit");
	stats("q9_after_trim_guest_reread", lat, n);
	residency("q9_after_reread", p, len, n);

	/* real pressure, when asked for: a host allocation touched page by page */
	emit("q9_pressure_mb", "%d", pressure_mb);
	if (pressure_mb > 0) {
		UINT64 plen = (UINT64) pressure_mb * MB, off;
		UINT8 *q = VirtualAlloc(NULL, plen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		emit("q9_pressure_commit", "%d", q ? 1 : 0);
		if (q) {
			t0 = now_ns();
			for (off = 0; off < plen; off += PAGE) q[off] = (UINT8) off;
			emit("q9_pressure_touch_ns", "%llu", (unsigned long long) (now_ns() - t0));
			residency("q9_under_pressure", p, len, n);
			emit("q9_under_pressure_working_set_kb", "%llu", (unsigned long long) (working_set() / 1024));
			ok = bad = 0;
			for (i = 0; i < n; i++) {
				UINT64 gpa = GPA_BIG + (len / n) * (UINT64) i;
				t0 = now_ns();
				r = guest_read(gpa, &val, 0);
				lat[i] = now_ns() - t0;
				if (r == WHvRunVpExitReasonX64Halt && val == (0x746167ULL ^ gpa)) ok++; else bad++;
			}
			emit("q9_under_pressure_guest_reads_correct", "%d", ok);
			emit("q9_under_pressure_guest_reads_other", "%d", bad);
			stats("q9_under_pressure_guest_reread", lat, n);
			VirtualFree(q, 0, MEM_RELEASE);
		}
	}
	hr = WHvUnmapGpaRange(part, GPA_BIG, len);
	emit("q9_unmap_hresult", "0x%08lx", (unsigned long) hr);
	VirtualFree(p, 0, MEM_RELEASE);
	free(lat);
}

/* ---- running a question in a child ----------------------------------- */

/* The child is this binary with --child <q>; its stdout is a pipe the parent
 * copies to its own, so the transcript reads the same whichever process wrote
 * a line. A child that dies leaves its lines behind and an exit code the
 * parent reports. */
static void run_child(const char *q)
{
	char exe[MAX_PATH], cmd[MAX_PATH + 64], key[64], buf[4096];
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	HANDLE rd = NULL, wr = NULL;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	DWORD got, code = 0, w;

	GetModuleFileNameA(NULL, exe, sizeof exe);
	snprintf(cmd, sizeof cmd, "\"%s\" --child %s --big-gb %d --commit-gb %d --pressure-mb %d",
	         exe, q, big_gb, commit_gb, pressure_mb);
	if (!CreatePipe(&rd, &wr, &sa, 0)) { emit("child_pipe", "0"); return; }
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = wr;
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	memset(&pi, 0, sizeof pi);
	if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
		sprintf(key, "%s_child", q);
		emit(key, "not-started:%lu", (unsigned long) GetLastError());
		CloseHandle(rd); CloseHandle(wr);
		return;
	}
	CloseHandle(wr);
	fflush(stdout);
	while (ReadFile(rd, buf, sizeof buf, &got, NULL) && got > 0) {
		fwrite(buf, 1, got, stdout);
		fflush(stdout);
	}
	CloseHandle(rd);
	w = WaitForSingleObject(pi.hProcess, 600000);
	if (w == WAIT_OBJECT_0)
		GetExitCodeProcess(pi.hProcess, &code);
	sprintf(key, "%s_child", q);
	if (w != WAIT_OBJECT_0)
		emit(key, "hung");
	else if (code == 0)
		emit(key, "exited-clean");
	else
		emit(key, "died:0x%08lx", (unsigned long) code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	UINT32 w = 0;
	const char *child = NULL;
	int i, present;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--child") && i + 1 < argc) { child = argv[++i]; continue; }
		if (!strcmp(argv[i], "--big-gb") && i + 1 < argc) { big_gb = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--commit-gb") && i + 1 < argc) { commit_gb = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--pressure-mb") && i + 1 < argc) { pressure_mb = atoi(argv[++i]); continue; }
		fprintf(stderr, "gpa-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);

	memset(&cap, 0, sizeof cap);
	present = SUCCEEDED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &w))
	          && cap.HypervisorPresent;
	if (!child)
		emit("hypervisor_present", "%d", present);
	if (!present)
		return 0;
	if (!bring_up()) {
		if (!child) emit("partition_ready", "0");
		return child ? 1 : 0;
	}
	if (!child)
		emit("partition_ready", "1");

	if (child) {
		if (!strcmp(child, "q4")) q4_reserve_only();
		else if (!strcmp(child, "q5")) q5_decommit_release();
		else if (!strcmp(child, "q8")) q8_reserve_at_scale();
		else if (!strcmp(child, "q9")) q9_committed_at_scale();
		return 0;
	}

	q1_capabilities();
	q2_map_committed();
	q3_guest_touch();
	run_child("q4");
	run_child("q5");
	q6_map_cost();
	q7_lazy_map_on_exit();
	run_child("q8");
	run_child("q9");

	WHvDeleteVirtualProcessor(part, 0);
	WHvDeletePartition(part);
	return 0;
}
