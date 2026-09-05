/* clone-host-probe: RtlCloneUserProcess of a Win32 process that owns a WHP
 * partition.
 *
 * Shape A of substrate H (one host process per Linux process) forks by cloning the host
 * process, as N does, and then builds the child a partition of its own. The
 * host under H is a Win32 process, because WinHvPlatform.dll needs kernel32,
 * and it holds a partition when it forks. Whether such a clone runs at all,
 * whether the inherited partition handle means anything in the child, whether
 * the child can create a partition and run a vCPU, and what the clone costs
 * with guest memory mapped: none of it had been measured. Spike 35 cloned a
 * plain process; this clones one holding the hypervisor.
 *
 * The child reports through a shared section and an inherited event, and
 * never touches the C runtime's stdio: a clone's runtime state is exactly what
 * is in question, so the child leans on nothing but Win32 and ntdll.
 *
 * Native, built with x86_64-w64-mingw32-gcc: the shape A host has no Cygwin
 * under it, so a Cygwin binary would be cloning the wrong process.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <winhvplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define RELEASE "clone-host-probe 1.0"

#define PAGE   0x1000ULL
#define MB     0x100000ULL

#define GPA_GDT    0x4000
#define GPA_TSS    0x5000
#define GPA_CODE   0x6000
#define GPA_STACK  0x8000
#define CONTROL    (4 * MB)
#define GPA_PML4   0x1000
#define GPA_PDPT   0x2000
#define GPA_PD     0x3000

#define RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES  0x00000002
#define RTL_CLONE_CHILD  297   /* STATUS_PROCESS_CLONED */
#define NT_CURRENT_PROCESS ((HANDLE)(LONG_PTR) -1)

typedef struct _RTL_USER_PROCESS_INFORMATION {
	ULONG Length;
	HANDLE Process;
	HANDLE Thread;
	CLIENT_ID ClientId;
	BYTE ImageInformation[64];
} RTL_USER_PROCESS_INFORMATION;

typedef NTSTATUS (NTAPI *fn_RtlCloneUserProcess)(ULONG, PVOID, PVOID, HANDLE, RTL_USER_PROCESS_INFORMATION *);
typedef NTSTATUS (NTAPI *fn_NtTerminateProcess)(HANDLE, NTSTATUS);
typedef NTSTATUS (NTAPI *fn_NtSetEvent)(HANDLE, PLONG);

static fn_RtlCloneUserProcess p_RtlCloneUserProcess;
static fn_NtTerminateProcess  p_NtTerminateProcess;
static fn_NtSetEvent          p_NtSetEvent;

/* What the child writes. Every field is a word so a torn read is impossible
 * and a missing step reads as its zero. */
struct report {
	UINT64 ran;                     /* CHILD_RAN once the child is executing */
	UINT64 step;                    /* last step reached, for a hang diagnosis */
	UINT64 inherited_run_hr;        /* WHvRunVirtualProcessor on the parent's handle */
	UINT64 inherited_run_reason;
	UINT64 create_hr, setup_hr, map_hr, vcpu_hr, regs_hr;
	UINT64 first_exit_reason;
	UINT64 create_to_exit_ns;
	UINT64 heap_ok, event_ok, wait_ok, loadlib_ok, virtualalloc_ok, tls_ok;
	UINT64 second_partition_hr;     /* a second partition in the child, mapped */
	UINT64 done;                    /* everything but the inherited-handle run */
	UINT64 inherited_done;          /* the inherited-handle run returned */
	UINT64 inherited_first;         /* set by the parent: run the inherited handle before anything else */
};
#define CHILD_RAN 0x4348494c4452414eULL

static volatile struct report *rep;
static HANDLE g_event;
static WHV_PARTITION_HANDLE g_part;
static LARGE_INTEGER freq;

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

/* ---- the guest: ring 0, identity 2 MB pages over 16 MB, a hlt loop ------- */

static void put64(UINT8 *m, UINT64 gpa, UINT64 v) { memcpy(m + gpa, &v, 8); }
static const UINT8 code_loop[] = { 0x48, 0xff, 0xc0, 0xf4, 0xeb, 0xfa };

static void build_control(UINT8 *m)
{
	int i;
	memset(m, 0, CONTROL);
	put64(m, GPA_PML4, GPA_PDPT | 0x7);
	put64(m, GPA_PDPT, GPA_PD | 0x7);
	for (i = 0; i < 8; i++)
		put64(m, GPA_PD + 8 * i, ((UINT64) i << 21) | 0x87);
	put64(m, GPA_GDT + 0x08, 0x00af9b000000ffffULL);
	put64(m, GPA_GDT + 0x10, 0x00cf93000000ffffULL);
	put64(m, GPA_GDT + 0x30, 0x0000890000000067ULL | ((UINT64)(GPA_TSS & 0xffffff) << 16)
	                         | ((UINT64)((GPA_TSS >> 24) & 0xff) << 56));
	put64(m, GPA_TSS + 0x04, GPA_STACK);
	memcpy(m + GPA_CODE, code_loop, sizeof code_loop);
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

static HRESULT enter(WHV_PARTITION_HANDLE p)
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
	n[i] = WHvX64RegisterRip;    v[i++].Reg64 = GPA_CODE;
	n[i] = WHvX64RegisterRax;    v[i++].Reg64 = 0;
	return WHvSetVirtualProcessorRegisters(p, 0, n, (UINT32) i, v);
}

static UINT32 run_once(WHV_PARTITION_HANDLE p, HRESULT *hr)
{
	WHV_RUN_VP_EXIT_CONTEXT ex;
	memset(&ex, 0, sizeof ex);
	*hr = WHvRunVirtualProcessor(p, 0, &ex, sizeof ex);
	return FAILED(*hr) ? 0xffffffffu : ex.ExitReason;
}

/* Bring a partition to its first halt: create, size, set up, map control
 * memory, create the vCPU, load registers, run. Each stage's HRESULT lands in
 * the fields given; returns the partition or NULL. */
static WHV_PARTITION_HANDLE bring_up(UINT8 *ctl, volatile UINT64 *create_hr, volatile UINT64 *setup_hr,
                                     volatile UINT64 *map_hr, volatile UINT64 *vcpu_hr,
                                     volatile UINT64 *regs_hr, volatile UINT64 *exit_reason)
{
	WHV_PARTITION_HANDLE p = NULL;
	WHV_PARTITION_PROPERTY prop;
	HRESULT hr;

	hr = WHvCreatePartition(&p);
	*create_hr = (UINT32) hr;
	if (FAILED(hr)) return NULL;
	memset(&prop, 0, sizeof prop);
	prop.ProcessorCount = 1;
	hr = WHvSetPartitionProperty(p, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof prop);
	if (SUCCEEDED(hr)) hr = WHvSetupPartition(p);
	*setup_hr = (UINT32) hr;
	if (FAILED(hr)) { WHvDeletePartition(p); return NULL; }
	hr = WHvMapGpaRange(p, ctl, 0, CONTROL,
	                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
	*map_hr = (UINT32) hr;
	if (FAILED(hr)) { WHvDeletePartition(p); return NULL; }
	hr = WHvCreateVirtualProcessor(p, 0, 0);
	*vcpu_hr = (UINT32) hr;
	if (FAILED(hr)) { WHvDeletePartition(p); return NULL; }
	hr = enter(p);
	*regs_hr = (UINT32) hr;
	if (FAILED(hr)) { WHvDeletePartition(p); return NULL; }
	*exit_reason = run_once(p, &hr);
	return p;
}

/* ---- the child ------------------------------------------------------------ */

static __attribute__((noreturn)) void child_main(void)
{
	UINT8 *ctl;
	HRESULT hr;
	UINT64 t0;
	WHV_PARTITION_HANDLE p2 = NULL, p3 = NULL;
	HANDLE ev, heap;
	void *mem;

	rep->ran = CHILD_RAN;
	rep->step = 1;

	/* 0. When the parent asks for it: the inherited partition handle as the
	 * child's very first WHP call. The first run of this probe did this by
	 * default and the call never returned; step 1 with nothing after it is
	 * that hang, and the parent reports it. */
	if (rep->inherited_first) {
		UINT32 reason = run_once(g_part, &hr);
		rep->inherited_run_hr = (UINT32) hr;
		rep->inherited_run_reason = reason;
		rep->inherited_done = 1;
	}

	/* 1. Ordinary Win32 in a clone: heap, event, wait, VirtualAlloc, TLS, and
	 * a DLL load, each of which touches state the clone copied. */
	heap = GetProcessHeap();
	mem = heap ? HeapAlloc(heap, 0, 4096) : NULL;
	rep->heap_ok = mem != NULL;
	if (mem) HeapFree(heap, 0, mem);
	ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	rep->event_ok = ev != NULL;
	rep->wait_ok = ev && WaitForSingleObject(ev, 1) == WAIT_TIMEOUT;
	mem = VirtualAlloc(NULL, CONTROL, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	rep->virtualalloc_ok = mem != NULL;
	{
		DWORD slot = TlsAlloc();
		rep->tls_ok = slot != TLS_OUT_OF_INDEXES && TlsSetValue(slot, (void *) 0x42) && TlsGetValue(slot) == (void *) 0x42;
	}
	rep->step = 3;
	rep->loadlib_ok = LoadLibraryW(L"version.dll") != NULL;
	rep->step = 4;

	/* 3. A partition of the child's own, all the way to a first exit. */
	ctl = mem;
	if (ctl) {
		build_control(ctl);
		t0 = now_ns();
		p2 = bring_up(ctl, &rep->create_hr, &rep->setup_hr, &rep->map_hr, &rep->vcpu_hr,
		              &rep->regs_hr, &rep->first_exit_reason);
		rep->create_to_exit_ns = now_ns() - t0;
	}
	rep->step = 5;

	/* 4. And a second one beside it, mapping fresh memory: the one-mapped-
	 * partition rule spike whp-partition-cost found, seen from a clone that
	 * also inherited the parent's mapped partition. */
	{
		WHV_PARTITION_PROPERTY prop;
		UINT8 *more = VirtualAlloc(NULL, 64 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		hr = WHvCreatePartition(&p3);
		if (SUCCEEDED(hr)) {
			memset(&prop, 0, sizeof prop);
			prop.ProcessorCount = 1;
			hr = WHvSetPartitionProperty(p3, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof prop);
		}
		if (SUCCEEDED(hr)) hr = WHvSetupPartition(p3);
		if (SUCCEEDED(hr) && more)
			hr = WHvMapGpaRange(p3, more, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
		rep->second_partition_hr = (UINT32) hr;
	}
	rep->step = 6;
	rep->done = 1;
	p_NtSetEvent(g_event, NULL);

	/* 5. Last, because the first run of this probe showed it never returns:
	 * the parent's partition handle, as inherited. The parent polls for the
	 * result and reports a hang if it does not arrive. */
	rep->step = 7;
	if (!rep->inherited_first) {
		UINT32 reason = run_once(g_part, &hr);
		rep->inherited_run_hr = (UINT32) hr;
		rep->inherited_run_reason = reason;
	}
	rep->step = 8;
	rep->inherited_done = 1;
	if (p2) WHvDeletePartition(p2);
	if (p3) WHvDeletePartition(p3);
	p_NtTerminateProcess(NT_CURRENT_PROCESS, 0);
	for (;;) Sleep(1000);
}

/* ---- the parent ------------------------------------------------------------ */

static void report_child(const char *tag, DWORD wait)
{
	char key[96];
#define K(name) (snprintf(key, sizeof key, "%s_%s", tag, name), key)
	emit(K("child"), "%s", wait == WAIT_OBJECT_0 ? (rep->done ? "reported" : "signalled-early")
	                        : wait == WAIT_TIMEOUT ? "hung" : "wait-failed");
	emit(K("child_ran"), "%d", rep->ran == CHILD_RAN);
	emit(K("child_last_step"), "%llu", (unsigned long long) rep->step);
	emit(K("inherited_handle_run_hresult"), "0x%08llx", (unsigned long long) rep->inherited_run_hr);
	emit(K("inherited_handle_run_reason"), "0x%llx", (unsigned long long) rep->inherited_run_reason);
	emit(K("heap_ok"), "%llu", (unsigned long long) rep->heap_ok);
	emit(K("event_ok"), "%llu", (unsigned long long) rep->event_ok);
	emit(K("wait_ok"), "%llu", (unsigned long long) rep->wait_ok);
	emit(K("virtualalloc_ok"), "%llu", (unsigned long long) rep->virtualalloc_ok);
	emit(K("tls_ok"), "%llu", (unsigned long long) rep->tls_ok);
	emit(K("loadlibrary_ok"), "%llu", (unsigned long long) rep->loadlib_ok);
	emit(K("partition_create_hresult"), "0x%08llx", (unsigned long long) rep->create_hr);
	emit(K("partition_setup_hresult"), "0x%08llx", (unsigned long long) rep->setup_hr);
	emit(K("partition_map_hresult"), "0x%08llx", (unsigned long long) rep->map_hr);
	emit(K("partition_vcpu_hresult"), "0x%08llx", (unsigned long long) rep->vcpu_hr);
	emit(K("partition_regs_hresult"), "0x%08llx", (unsigned long long) rep->regs_hr);
	emit(K("partition_first_exit_reason"), "0x%llx", (unsigned long long) rep->first_exit_reason);
	emit(K("partition_first_exit_halt"), "%d", rep->first_exit_reason == WHvRunVpExitReasonX64Halt);
	emit(K("partition_create_to_exit_ns"), "%llu", (unsigned long long) rep->create_to_exit_ns);
	emit(K("second_partition_map_hresult"), "0x%08llx", (unsigned long long) rep->second_partition_hr);
#undef K
}

/* One clone whose child does the full report; the parent waits on the event
 * and prints what came back. */
static void q2_clone_with_partition(const char *tag, int inherited_first)
{
	RTL_USER_PROCESS_INFORMATION info;
	NTSTATUS s;
	DWORD w;

	memset((void *) rep, 0, sizeof *rep);
	rep->inherited_first = (UINT64) inherited_first;
	ResetEvent(g_event);
	memset(&info, 0, sizeof info);
	s = p_RtlCloneUserProcess(RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES, NULL, NULL, NULL, &info);
	if (s == RTL_CLONE_CHILD)
		child_main();
	{
		char key[64];
		snprintf(key, sizeof key, "%s_clone_status", tag);
		emit(key, "0x%08lx", (unsigned long) s);
	}
	if (s < 0) return;
	w = WaitForSingleObject(g_event, inherited_first ? 8000 : 20000);
	if (w == WAIT_OBJECT_0) {
		/* the child is now inside WHvRunVirtualProcessor on the inherited
		 * handle; give it five seconds to come back */
		int k;
		for (k = 0; k < 500 && !rep->inherited_done; k++) Sleep(10);
	}
	report_child(tag, w);
	{
		char key[64];
		snprintf(key, sizeof key, "%s_inherited_handle_run", tag);
		emit(key, "%s", rep->inherited_done ? "returned"
		                : (inherited_first && rep->step == 1) || rep->step >= 7 ? "hangs" : "not-reached");
	}
	if (w != WAIT_OBJECT_0 || !rep->inherited_done) TerminateProcess(info.Process, 1);
	WaitForSingleObject(info.Process, 5000);
	CloseHandle(info.Process);
	CloseHandle(info.Thread);
}

/* Time the clone alone: the child terminates at once. */
static UINT64 time_clones(int n, int *failures)
{
	UINT64 *lat = malloc((size_t) n * sizeof *lat), t0, med;
	int i, kept = 0;
	*failures = 0;
	for (i = 0; i < n; i++) {
		RTL_USER_PROCESS_INFORMATION info;
		NTSTATUS s;
		memset(&info, 0, sizeof info);
		t0 = now_ns();
		s = p_RtlCloneUserProcess(RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES, NULL, NULL, NULL, &info);
		if (s == RTL_CLONE_CHILD) {
			p_NtTerminateProcess(NT_CURRENT_PROCESS, 0);
			for (;;) Sleep(1000);
		}
		if (s < 0) { (*failures)++; continue; }
		lat[kept++] = now_ns() - t0;
		p_NtTerminateProcess(info.Process, 0);
		CloseHandle(info.Process);
		CloseHandle(info.Thread);
	}
	if (kept == 0) { free(lat); return 0; }
	qsort(lat, (size_t) kept, sizeof *lat, cmp_u64);
	med = lat[kept / 2];
	free(lat);
	return med;
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	UINT32 w = 0;
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	HANDLE sec;
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	UINT8 *ctl, *big;
	UINT64 c_hr, s_hr, m_hr, v_hr, r_hr, reason, med;
	HRESULT hr;
	int i, present, iters = 20, failures;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc) { iters = atoi(argv[++i]); continue; }
		fprintf(stderr, "clone-host-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);

	p_RtlCloneUserProcess = (fn_RtlCloneUserProcess) (void *) GetProcAddress(nt, "RtlCloneUserProcess");
	p_NtTerminateProcess  = (fn_NtTerminateProcess)  (void *) GetProcAddress(nt, "NtTerminateProcess");
	p_NtSetEvent          = (fn_NtSetEvent)          (void *) GetProcAddress(nt, "NtSetEvent");
	emit("rtlclone_available", "%d", p_RtlCloneUserProcess != NULL);

	memset(&cap, 0, sizeof cap);
	present = SUCCEEDED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &w))
	          && cap.HypervisorPresent;
	emit("hypervisor_present", "%d", present);
	if (!present || !p_RtlCloneUserProcess) return 0;

	/* q1. The parent: a Win32 process (kernel32 is here by construction) with
	 * WinHvPlatform loaded and a partition run to its first halt. */
	emit("q1_kernel32_loaded", "%d", GetModuleHandleW(L"kernel32.dll") != NULL);
	emit("q1_winhvplatform_loaded", "%d", GetModuleHandleW(L"WinHvPlatform.dll") != NULL);
	ctl = VirtualAlloc(NULL, CONTROL, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!ctl) return 1;
	build_control(ctl);
	g_part = bring_up(ctl, &c_hr, &s_hr, &m_hr, &v_hr, &r_hr, &reason);
	emit("q1_parent_partition_first_exit_halt", "%d", g_part && reason == WHvRunVpExitReasonX64Halt);
	if (!g_part) { emit("q1_parent_partition_hresults", "0x%llx,0x%llx,0x%llx,0x%llx,0x%llx",
	                    (unsigned long long) c_hr, (unsigned long long) s_hr, (unsigned long long) m_hr,
	                    (unsigned long long) v_hr, (unsigned long long) r_hr); return 0; }

	sec = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 4096, NULL);
	rep = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
	g_event = CreateEventW(&sa, TRUE, FALSE, NULL);
	if (!rep || !g_event) { emit("setup_shared", "0"); return 1; }

	/* q2. Clone with the partition held and 4 MB mapped: first a child whose
	 * first WHP call is on the inherited handle, then one that builds its own
	 * partition first and touches the inherited handle last. */
	q2_clone_with_partition("q2a", 1);
	q2_clone_with_partition("q2", 0);

	/* q3. The parent's partition afterwards: still runs? */
	{
		UINT32 r = run_once(g_part, &hr);
		emit("q3_parent_partition_runs_after_clone", "%d", r == WHvRunVpExitReasonX64Halt);
		emit("q3_parent_run_hresult", "0x%08lx", (unsigned long) hr);
	}

	/* q4. Clone timing, small mapping: the shape A fork floor before the
	 * child rebuilds anything. */
	med = time_clones(iters, &failures);
	emit("q4_clone_iterations", "%d", iters);
	emit("q4_clone_failures", "%d", failures);
	emit("q4_clone_median_ns_4mb_mapped", "%llu", (unsigned long long) med);

	/* q5. The same with 256 MB of touched guest memory mapped: does what the
	 * partition holds change what the clone costs, or whether it works. */
	big = VirtualAlloc(NULL, 256 * MB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (big) {
		memset(big, 1, 256 * MB);
		hr = WHvMapGpaRange(g_part, big, 0x40000000ULL, 256 * MB, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
		emit("q5_map_256mb_hresult", "0x%08lx", (unsigned long) hr);
		med = time_clones(iters, &failures);
		emit("q5_clone_failures", "%d", failures);
		emit("q5_clone_median_ns_260mb_mapped", "%llu", (unsigned long long) med);
		/* and a full report from a child once more, with the big mapping held */
		q2_clone_with_partition("q5", 0);
		{
			UINT32 r = run_once(g_part, &hr);
			emit("q5_parent_partition_runs_after", "%d", r == WHvRunVpExitReasonX64Halt);
		}
		/* the control: the same 256 MB, touched, no longer mapped. If the
		 * clone still costs what q5 did, the price is NT's for touched pages
		 * and the hypervisor added nothing. */
		WHvUnmapGpaRange(g_part, 0x40000000ULL, 256 * MB);
		med = time_clones(iters, &failures);
		emit("q5b_clone_failures", "%d", failures);
		emit("q5b_clone_median_ns_256mb_touched_unmapped", "%llu", (unsigned long long) med);
	} else {
		emit("q5_map_256mb_hresult", "no-memory");
	}

	WHvDeleteVirtualProcessor(g_part, 0);
	WHvDeletePartition(g_part);
	return 0;
}
