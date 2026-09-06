/* partition-probe: what a WHP partition and a vCPU cost, and how many of each
 * one process may hold.
 *
 * Shape A of substrate H (one host process per Linux process) puts a partition under every
 * Linux process, so a fork builds one and a parallel build holds hundreds.
 * Shape B pools vCPUs in one partition and hands them between threads. Both
 * rest on numbers nobody has taken: partition create-to-first-run time, the
 * ceilings on partitions per process and vCPUs per partition, whether a vCPU
 * may be run from one thread and then another, and whether exits from several
 * vCPUs at once cost what one does.
 *
 * Six questions, key=value on stdout; measure.sh states the verdict. Built with
 * Cygwin's gcc against w32api's winhvplatform, like spike/whp.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <winhvplatform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE "partition-probe 1.1"

#define PAGE   0x1000ULL
#define MB     0x100000ULL

#define GPA_PML4   0x1000
#define GPA_PDPT   0x2000
#define GPA_PD     0x3000
#define GPA_GDT    0x4000
#define GPA_TSS    0x5000
#define GPA_CODE   0x6000
#define GPA_STACK  0x8000
#define CONTROL    (4 * MB)

static int verbose;

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

static UINT64 median(UINT64 *lat, size_t n)
{
	if (n == 0) return 0;
	qsort(lat, n, sizeof *lat, cmp_u64);
	return lat[n / 2];
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

static UINT64 private_kb(void)
{
	PROCESS_MEMORY_COUNTERS_EX pmc;
	memset(&pmc, 0, sizeof pmc);
	pmc.cb = sizeof pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *) &pmc, sizeof pmc);
	return pmc.PrivateUsage / 1024;
}

/* ---- the guest: identity 2 MB pages over 16 MB, a hlt loop at ring 0 --- */

static void put64(UINT8 *m, UINT64 gpa, UINT64 v) { memcpy(m + gpa, &v, 8); }

/* inc %rax; hlt; jmp back */
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

static HRESULT enter(WHV_PARTITION_HANDLE p, UINT32 vp)
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
	return WHvSetVirtualProcessorRegisters(p, vp, n, (UINT32) i, v);
}

static int run_to_halt(WHV_PARTITION_HANDLE p, UINT32 vp)
{
	WHV_RUN_VP_EXIT_CONTEXT ex;
	memset(&ex, 0, sizeof ex);
	if (FAILED(WHvRunVirtualProcessor(p, vp, &ex, sizeof ex)))
		return 0;
	return ex.ExitReason == WHvRunVpExitReasonX64Halt;
}

static const char *make_stage;   /* where the last make_partition stopped */

static HRESULT make_partition(WHV_PARTITION_HANDLE *p, UINT32 vcpus)
{
	WHV_PARTITION_PROPERTY prop;
	HRESULT hr = WHvCreatePartition(p);
	make_stage = "create";
	if (FAILED(hr)) return hr;
	memset(&prop, 0, sizeof prop);
	prop.ProcessorCount = vcpus;
	make_stage = "processor-count";
	hr = WHvSetPartitionProperty(*p, WHvPartitionPropertyCodeProcessorCount, &prop, sizeof prop);
	if (SUCCEEDED(hr)) {
		make_stage = "setup";
		hr = WHvSetupPartition(*p);
	}
	if (FAILED(hr)) { WHvDeletePartition(*p); *p = NULL; }
	else make_stage = "ok";
	return hr;
}

/* ---- q1: create, set up, delete ---------------------------------------- */

static void q1_create_setup(void)
{
	enum { n = 40 };
	UINT64 lat[n], del[n], t0;
	int i, ok = 0;
	for (i = 0; i < n; i++) {
		WHV_PARTITION_HANDLE p = NULL;
		t0 = now_ns();
		if (SUCCEEDED(make_partition(&p, 1))) ok++;
		lat[i] = now_ns() - t0;
		t0 = now_ns();
		if (p) WHvDeletePartition(p);
		del[i] = now_ns() - t0;
	}
	emit("q1_created", "%d", ok);
	stats("q1_create_setup", lat, n);
	stats("q1_delete", del, n);
}

/* ---- q2: the whole way to the first exit ------------------------------- */

static void q2_first_run(UINT8 *ctl)
{
	enum { n = 40 };
	UINT64 lat[n], t0;
	int i, ok = 0;
	for (i = 0; i < n; i++) {
		WHV_PARTITION_HANDLE p = NULL;
		int good = 0;
		t0 = now_ns();
		if (SUCCEEDED(make_partition(&p, 1))
		    && SUCCEEDED(WHvMapGpaRange(p, ctl, 0, CONTROL,
		                                WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute))
		    && SUCCEEDED(WHvCreateVirtualProcessor(p, 0, 0))
		    && SUCCEEDED(enter(p, 0))
		    && run_to_halt(p, 0))
			good = 1;
		lat[i] = now_ns() - t0;
		ok += good;
		if (p) { WHvDeleteVirtualProcessor(p, 0); WHvUnmapGpaRange(p, 0, CONTROL); WHvDeletePartition(p); }
	}
	emit("q2_first_run_ok", "%d", ok);
	stats("q2_create_to_first_exit", lat, n);
}

/* ---- q3: partitions per process ---------------------------------------- */

static void q3_partitions_per_process(int cap)
{
	WHV_PARTITION_HANDLE *ps = calloc((size_t) cap, sizeof *ps);
	UINT64 *lat = calloc((size_t) cap, sizeof *lat);
	UINT64 kb0 = private_kb(), t0, head[10], tail[10];
	HRESULT hr = S_OK, last = S_OK;
	const char *stage = "none";
	int i, count = 0;

	UINT8 *bufs;

	if (!ps || !lat) return;
	/* one 64 KB host range per partition: the same range cannot be mapped
	 * into two partitions (q3b measures that), so sharing ctl would stop
	 * the count at one for the wrong reason */
	bufs = VirtualAlloc(NULL, (UINT64) cap * 64 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!bufs) { emit("q3_partitions_held", "0"); return; }
	for (i = 0; i < cap; i++) {
		t0 = now_ns();
		hr = make_partition(&ps[i], 1);
		stage = make_stage;
		if (SUCCEEDED(hr)) {
			stage = "map";
			hr = WHvMapGpaRange(ps[i], bufs + (UINT64) i * 64 * 1024, 0, 64 * 1024,
			                    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute);
		}
		if (SUCCEEDED(hr)) {
			stage = "vcpu";
			hr = WHvCreateVirtualProcessor(ps[i], 0, 0);
		}
		lat[i] = now_ns() - t0;
		if (FAILED(hr)) { last = hr; if (ps[i]) WHvDeletePartition(ps[i]); ps[i] = NULL; break; }
		count++;
	}
	emit("q3_partitions_held", "%d", count);
	emit("q3_cap", "%d", cap);
	emit("q3_stopped_by", "%s", count == cap ? "cap" : "refusal");
	emit("q3_refusal_hresult", "0x%08lx", (unsigned long) (count == cap ? 0 : last));
	emit("q3_refusal_stage", "%s", count == cap ? "none" : stage);

	/* Is the ceiling per process or per host? A second process holding its
	 * own partition while this one holds `count` tells the two apart. */
	if (count >= 1) {
		char exe[MAX_PATH], cmd[MAX_PATH + 32], buf[64];
		SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
		HANDLE rd = NULL, wr = NULL;
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		DWORD got = 0, code = 99;

		GetModuleFileNameA(NULL, exe, sizeof exe);
		snprintf(cmd, sizeof cmd, "\"%s\" --hold", exe);
		CreatePipe(&rd, &wr, &sa, 0);
		SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
		memset(&si, 0, sizeof si);
		si.cb = sizeof si;
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = wr;
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		memset(&pi, 0, sizeof pi);
		if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
			CloseHandle(wr);
			memset(buf, 0, sizeof buf);
			ReadFile(rd, buf, sizeof buf - 1, &got, NULL);   /* "held" or "refused:0x..." */
			buf[strcspn(buf, "\r\n")] = 0;
			emit("q3_second_process", "%s", got ? buf : "silent");
			WaitForSingleObject(pi.hProcess, 30000);
			GetExitCodeProcess(pi.hProcess, &code);
			emit("q3_second_process_exit", "%lu", (unsigned long) code);
			CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
		} else {
			emit("q3_second_process", "not-started");
			CloseHandle(wr);
		}
		CloseHandle(rd);
	}
	emit("q3_private_kb_per_partition", "%llu",
	     count ? (unsigned long long) ((private_kb() - kb0) / (UINT64) count) : 0ULL);
	if (count >= 20) {
		for (i = 0; i < 10; i++) { head[i] = lat[i]; tail[i] = lat[count - 10 + i]; }
		emit("q3_first_ten_median_ns", "%llu", (unsigned long long) median(head, 10));
		emit("q3_last_ten_median_ns", "%llu", (unsigned long long) median(tail, 10));
	}
	t0 = now_ns();
	for (i = 0; i < count; i++) {
		WHvDeleteVirtualProcessor(ps[i], 0);
		WHvDeletePartition(ps[i]);
	}
	emit("q3_delete_all_ns", "%llu", (unsigned long long) (now_ns() - t0));
	VirtualFree(bufs, 0, MEM_RELEASE);
	free(ps);
	free(lat);

	/* q3b. Two set-up partitions in one process: can the second map memory at
	 * all, and does the order matter? Then the case a second Linux process
	 * needs under shape B1: memory both guests see. */
	{
		WHV_PARTITION_HANDLE a = NULL, b = NULL;
		UINT8 *two = VirtualAlloc(NULL, 128 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		HRESULT h1, h2;
		if (two && SUCCEEDED(make_partition(&a, 1)) && SUCCEEDED(make_partition(&b, 1))) {
			emit("q3b_two_partitions_set_up", "1");
			/* b first, then a: if only the first mapper wins, a is refused now */
			h2 = WHvMapGpaRange(b, two + 64 * 1024, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			h1 = WHvMapGpaRange(a, two, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			emit("q3b_second_created_maps_first", "0x%08lx", (unsigned long) h2);
			emit("q3b_first_created_maps_second", "0x%08lx", (unsigned long) h1);
			emit("q3b_two_partitions_both_map", "%s", SUCCEEDED(h1) && SUCCEEDED(h2) ? "accepted" : "refused");
			if (SUCCEEDED(h2)) WHvUnmapGpaRange(b, 0, 64 * 1024);
			if (SUCCEEDED(h1)) WHvUnmapGpaRange(a, 0, 64 * 1024);
			/* after the first mapper unmaps, may the other map? */
			h1 = WHvMapGpaRange(a, two, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			emit("q3b_other_maps_after_unmap", "%s:0x%08lx", SUCCEEDED(h1) ? "accepted" : "refused", (unsigned long) h1);
			if (SUCCEEDED(h1)) WHvUnmapGpaRange(a, 0, 64 * 1024);
		} else {
			emit("q3b_two_partitions_set_up", "0");
		}
		if (a) WHvDeletePartition(a);
		if (b) WHvDeletePartition(b);
		if (two) VirtualFree(two, 0, MEM_RELEASE);
	}

	/* q3c. One section, two processes, two partitions: shape A's MAP_SHARED.
	 * This process maps a view of a named section into its partition; a
	 * child does the same with its own view; a write through this view is
	 * checked in the child's. */
	{
		WHV_PARTITION_HANDLE a = NULL;
		char name[64], exe[MAX_PATH], cmd[MAX_PATH + 96], buf[96];
		HANDLE sec, rd = NULL, wr = NULL;
		UINT8 *v1;
		HRESULT h1;
		SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		DWORD got = 0, code = 99;

		snprintf(name, sizeof name, "Local\\whp-partition-cost-%lu", (unsigned long) GetCurrentProcessId());
		sec = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64 * 1024, name);
		v1 = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
		if (v1 && SUCCEEDED(make_partition(&a, 1))) {
			memset(v1, 0, 64 * 1024);
			h1 = WHvMapGpaRange(a, v1, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			emit("q3c_parent_maps_view", "0x%08lx", (unsigned long) h1);
			*(UINT64 *) (v1 + 0x100) = 0x5348415245ULL;
			GetModuleFileNameA(NULL, exe, sizeof exe);
			snprintf(cmd, sizeof cmd, "\"%s\" --hold-shared %s", exe, name);
			CreatePipe(&rd, &wr, &sa, 0);
			SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
			memset(&si, 0, sizeof si);
			si.cb = sizeof si;
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = wr;
			si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
			memset(&pi, 0, sizeof pi);
			if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
				CloseHandle(wr);
				memset(buf, 0, sizeof buf);
				ReadFile(rd, buf, sizeof buf - 1, &got, NULL);
				buf[strcspn(buf, "\r\n")] = 0;
				emit("q3c_child_maps_own_view", "%s", got ? buf : "silent");
				WaitForSingleObject(pi.hProcess, 30000);
				GetExitCodeProcess(pi.hProcess, &code);
				emit("q3c_child_exit", "%lu", (unsigned long) code);
				CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
			} else {
				emit("q3c_child_maps_own_view", "not-started");
				CloseHandle(wr);
			}
			CloseHandle(rd);
			if (SUCCEEDED(h1)) WHvUnmapGpaRange(a, 0, 64 * 1024);
		} else {
			emit("q3c_parent_maps_view", "setup-failed");
		}
		if (a) WHvDeletePartition(a);
		if (v1) UnmapViewOfFile(v1);
		if (sec) CloseHandle(sec);
	}
}

/* ---- q4: vCPUs per partition ------------------------------------------- */

static void q4_vcpus_per_partition(UINT8 *ctl, int create_cap)
{
	static const UINT32 tries[] = { 1, 2, 4, 8, 16, 32, 64, 128, 240, 256, 512, 1024, 2048 };
	WHV_PARTITION_HANDLE p = NULL;
	UINT32 max_ok = 0, count;
	UINT64 kb0, t0, *lat;
	HRESULT hr = S_OK;
	unsigned i;
	char list[256] = "";

	for (i = 0; i < sizeof tries / sizeof *tries; i++) {
		hr = make_partition(&p, tries[i]);
		if (SUCCEEDED(hr)) { max_ok = tries[i]; WHvDeletePartition(p); p = NULL; }
		else {
			char one[32];
			snprintf(one, sizeof one, "%s%u:0x%08lx", *list ? ";" : "", (unsigned) tries[i], (unsigned long) hr);
			strncat(list, one, sizeof list - strlen(list) - 1);
		}
	}
	emit("q4_processor_count_max_accepted", "%u", (unsigned) max_ok);
	emit("q4_processor_count_refused", "%s", *list ? list : "none");
	if (!max_ok) return;

	count = max_ok < (UINT32) create_cap ? max_ok : (UINT32) create_cap;
	if (FAILED(make_partition(&p, count))) { emit("q4_vcpus_created", "0"); return; }
	if (FAILED(WHvMapGpaRange(p, ctl, 0, CONTROL,
	                          WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute))) {
		emit("q4_vcpus_created", "0"); WHvDeletePartition(p); return;
	}
	lat = calloc(count, sizeof *lat);
	kb0 = private_kb();
	for (i = 0; i < count; i++) {
		t0 = now_ns();
		hr = WHvCreateVirtualProcessor(p, i, 0);
		lat[i] = now_ns() - t0;
		if (FAILED(hr)) break;
	}
	emit("q4_vcpus_attempted", "%u", (unsigned) count);
	emit("q4_vcpus_created", "%u", (unsigned) i);
	emit("q4_vcpu_create_refusal_hresult", "0x%08lx", (unsigned long) (i == count ? 0 : hr));
	emit("q4_private_kb_per_vcpu", "%llu", i ? (unsigned long long) ((private_kb() - kb0) / i) : 0ULL);
	stats("q4_vcpu_create", lat, i);
	/* every created vCPU runs to its first halt: they all work, not just exist */
	{
		unsigned ran = 0, k;
		for (k = 0; k < i; k++)
			if (SUCCEEDED(enter(p, k)) && run_to_halt(p, k)) ran++;
		emit("q4_vcpus_ran_to_halt", "%u", ran);
		for (k = 0; k < i; k++) WHvDeleteVirtualProcessor(p, k);
	}
	WHvUnmapGpaRange(p, 0, CONTROL);
	WHvDeletePartition(p);
	free(lat);
}

/* ---- q5: one vCPU, two threads, alternating ---------------------------- */

struct handoff {
	WHV_PARTITION_HANDLE p;
	HANDLE my_turn, their_turn;
	int rounds, halts, run_failures;
};

static DWORD WINAPI handoff_thread(LPVOID arg)
{
	struct handoff *h = arg;
	int i;
	for (i = 0; i < h->rounds; i++) {
		WaitForSingleObject(h->my_turn, INFINITE);
		if (run_to_halt(h->p, 0)) h->halts++; else h->run_failures++;
		SetEvent(h->their_turn);
	}
	return 0;
}

static void q5_thread_handoff(UINT8 *ctl)
{
	WHV_PARTITION_HANDLE p = NULL;
	struct handoff a, b;
	HANDLE ta, tb, ea, eb;
	WHV_REGISTER_NAME rn = WHvX64RegisterRax;
	WHV_REGISTER_VALUE rv;
	enum { rounds = 1000 };

	if (FAILED(make_partition(&p, 1))
	    || FAILED(WHvMapGpaRange(p, ctl, 0, CONTROL,
	                             WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute))
	    || FAILED(WHvCreateVirtualProcessor(p, 0, 0)) || FAILED(enter(p, 0))) {
		emit("q5_setup", "0"); return;
	}
	emit("q5_setup", "1");
	ea = CreateEvent(NULL, FALSE, TRUE, NULL);    /* A goes first */
	eb = CreateEvent(NULL, FALSE, FALSE, NULL);
	memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
	a.p = b.p = p; a.rounds = b.rounds = rounds;
	a.my_turn = ea; a.their_turn = eb;
	b.my_turn = eb; b.their_turn = ea;
	ta = CreateThread(NULL, 0, handoff_thread, &a, 0, NULL);
	tb = CreateThread(NULL, 0, handoff_thread, &b, 0, NULL);
	if (WaitForSingleObject(ta, 30000) != WAIT_OBJECT_0 || WaitForSingleObject(tb, 30000) != WAIT_OBJECT_0)
		emit("q5_handoff", "hung");
	else
		emit("q5_handoff", "%s", (a.halts + b.halts == 2 * rounds) ? "alternates" : "failed");
	emit("q5_halts_a", "%d", a.halts);
	emit("q5_halts_b", "%d", b.halts);
	emit("q5_run_failures", "%d", a.run_failures + b.run_failures);
	memset(&rv, 0, sizeof rv);
	WHvGetVirtualProcessorRegisters(p, 0, &rn, 1, &rv);
	emit("q5_loop_count_rax", "%llu", (unsigned long long) rv.Reg64);
	CloseHandle(ta); CloseHandle(tb); CloseHandle(ea); CloseHandle(eb);
	WHvDeleteVirtualProcessor(p, 0);
	WHvUnmapGpaRange(p, 0, CONTROL);
	WHvDeletePartition(p);
}

/* ---- q6: exits from several vCPUs at once ------------------------------ */

struct runner {
	WHV_PARTITION_HANDLE p;
	UINT32 vp;
	int exits;
	UINT64 *lat;
	UINT64 median_ns, total_ns;
	int failures;
	HANDLE go;
};

static DWORD WINAPI runner_thread(LPVOID arg)
{
	struct runner *r = arg;
	UINT64 t0, tall;
	int i;
	WaitForSingleObject(r->go, INFINITE);
	tall = now_ns();
	for (i = 0; i < r->exits; i++) {
		t0 = now_ns();
		if (!run_to_halt(r->p, r->vp)) { r->failures++; break; }
		r->lat[i] = now_ns() - t0;
	}
	r->total_ns = now_ns() - tall;
	r->median_ns = median(r->lat, (size_t) i);
	return 0;
}

static void q6_concurrent_exits(UINT8 *ctl, int nvcpu, int exits)
{
	WHV_PARTITION_HANDLE p = NULL;
	struct runner *rs = calloc((size_t) nvcpu, sizeof *rs);
	HANDLE *ts = calloc((size_t) nvcpu, sizeof *ts);
	HANDLE go = CreateEvent(NULL, TRUE, FALSE, NULL);
	UINT64 lo = ~0ULL, hi = 0, sum_exits = 0, longest = 0, single_median;
	int i, failures = 0;

	if (!rs || !ts || FAILED(make_partition(&p, (UINT32) nvcpu))
	    || FAILED(WHvMapGpaRange(p, ctl, 0, CONTROL,
	                             WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute))) {
		emit("q6_setup", "0"); return;
	}
	for (i = 0; i < nvcpu; i++) {
		if (FAILED(WHvCreateVirtualProcessor(p, (UINT32) i, 0)) || FAILED(enter(p, (UINT32) i))) {
			emit("q6_setup", "0"); return;
		}
	}
	emit("q6_setup", "1");
	emit("q6_vcpus", "%d", nvcpu);
	emit("q6_exits_per_vcpu", "%d", exits);

	/* single-vCPU baseline in this partition, same code, one thread */
	rs[0].p = p; rs[0].vp = 0; rs[0].exits = exits; rs[0].lat = calloc((size_t) exits, sizeof(UINT64)); rs[0].go = go;
	SetEvent(go);
	runner_thread(&rs[0]);
	ResetEvent(go);
	single_median = rs[0].median_ns;
	emit("q6_single_vcpu_median_ns", "%llu", (unsigned long long) single_median);
	emit("q6_single_vcpu_failures", "%d", rs[0].failures);

	for (i = 0; i < nvcpu; i++) {
		rs[i].p = p; rs[i].vp = (UINT32) i; rs[i].exits = exits; rs[i].go = go;
		rs[i].failures = 0; rs[i].median_ns = 0; rs[i].total_ns = 0;
		if (!rs[i].lat) rs[i].lat = calloc((size_t) exits, sizeof(UINT64));
		ts[i] = CreateThread(NULL, 0, runner_thread, &rs[i], 0, NULL);
	}
	Sleep(50);
	SetEvent(go);
	for (i = 0; i < nvcpu; i++) {
		if (WaitForSingleObject(ts[i], 60000) != WAIT_OBJECT_0) { emit("q6_concurrent", "hung"); return; }
		CloseHandle(ts[i]);
	}
	for (i = 0; i < nvcpu; i++) {
		if (rs[i].median_ns < lo) lo = rs[i].median_ns;
		if (rs[i].median_ns > hi) hi = rs[i].median_ns;
		if (rs[i].total_ns > longest) longest = rs[i].total_ns;
		sum_exits += (UINT64) (exits - rs[i].failures);
		failures += rs[i].failures;
	}
	emit("q6_concurrent", "%s", failures ? "failed" : "ran");
	emit("q6_concurrent_failures", "%d", failures);
	emit("q6_concurrent_median_ns_min", "%llu", (unsigned long long) lo);
	emit("q6_concurrent_median_ns_max", "%llu", (unsigned long long) hi);
	emit("q6_aggregate_exits_per_second", "%llu",
	     longest ? (unsigned long long) (sum_exits * 1000000000ULL / longest) : 0ULL);
	emit("q6_single_exits_per_second", "%llu",
	     single_median ? (unsigned long long) (1000000000ULL / single_median) : 0ULL);
	for (i = 0; i < nvcpu; i++) { WHvDeleteVirtualProcessor(p, (UINT32) i); free(rs[i].lat); }
	WHvUnmapGpaRange(p, 0, CONTROL);
	WHvDeletePartition(p);
	CloseHandle(go);
	free(rs); free(ts);
}

/* ---- q7: a vCPU pool under many threads --------------------------------- */

/* Proposal 0012's H runs every Linux thread on a host thread that borrows a
 * vCPU from a pool the size of the host, loads the thread's registers,
 * runs to the next exit, saves them, and gives the vCPU back. Spike 43's q5
 * showed two threads can share one vCPU; this is the pool as designed, K
 * vCPUs under T threads, each thread keeping a private counter the guest
 * increments once per run. A counter that ever advances by anything but one
 * is a thread that ran on another thread's registers. */

struct pool {
	WHV_PARTITION_HANDLE p;
	int k, top;
	int *free_stack;
	HANDLE sem;
	CRITICAL_SECTION cs;
};

static int pool_get(struct pool *pl)
{
	int v;
	WaitForSingleObject(pl->sem, INFINITE);
	EnterCriticalSection(&pl->cs);
	v = pl->free_stack[--pl->top];
	LeaveCriticalSection(&pl->cs);
	return v;
}

static void pool_put(struct pool *pl, int v)
{
	EnterCriticalSection(&pl->cs);
	pl->free_stack[pl->top++] = v;
	LeaveCriticalSection(&pl->cs);
	ReleaseSemaphore(pl->sem, 1, NULL);
}

struct pool_thread {
	struct pool *pl;
	int rounds, halts, failures, wrong;
	UINT64 counter, total_ns;
	UINT64 *lat;
	unsigned char used[256];
	HANDLE go;
};

static DWORD WINAPI pool_thread_main(LPVOID arg)
{
	struct pool_thread *t = arg;
	WHV_REGISTER_NAME n[2] = { WHvX64RegisterRip, WHvX64RegisterRax };
	WHV_REGISTER_VALUE v[2];
	WHV_RUN_VP_EXIT_CONTEXT ex;
	UINT64 t0, tall;
	int i;

	WaitForSingleObject(t->go, INFINITE);
	tall = now_ns();
	for (i = 0; i < t->rounds; i++) {
		int vp;
		t0 = now_ns();
		vp = pool_get(t->pl);
		if (vp < 256) t->used[vp] = 1;
		/* load: this thread's registers onto the borrowed vCPU */
		memset(v, 0, sizeof v);
		v[0].Reg64 = GPA_CODE; v[1].Reg64 = t->counter;
		if (FAILED(WHvSetVirtualProcessorRegisters(t->pl->p, (UINT32) vp, n, 2, v))) { t->failures++; pool_put(t->pl, vp); continue; }
		memset(&ex, 0, sizeof ex);
		if (FAILED(WHvRunVirtualProcessor(t->pl->p, (UINT32) vp, &ex, sizeof ex)) || ex.ExitReason != WHvRunVpExitReasonX64Halt) {
			t->failures++; pool_put(t->pl, vp); continue;
		}
		/* save: the registers back, the vCPU returned */
		memset(v, 0, sizeof v);
		if (FAILED(WHvGetVirtualProcessorRegisters(t->pl->p, (UINT32) vp, &n[1], 1, &v[1]))) { t->failures++; pool_put(t->pl, vp); continue; }
		pool_put(t->pl, vp);
		if (v[1].Reg64 != t->counter + 1) t->wrong++;
		t->counter = v[1].Reg64;
		t->halts++;
		t->lat[i] = now_ns() - t0;
	}
	t->total_ns = now_ns() - tall;
	return 0;
}

static const char *pk(const char *pfx, const char *k)
{
	static char buf[96];
	snprintf(buf, sizeof buf, "%s_%s", pfx, k);
	return buf;
}

static void q7_vcpu_pool(const char *pfx, UINT8 *ctl, int k, int nthreads, int rounds)
{
	WHV_PARTITION_HANDLE p = NULL;
	struct pool pl;
	struct pool_thread *ts = calloc((size_t) nthreads, sizeof *ts);
	HANDLE *hs = calloc((size_t) nthreads, sizeof *hs);
	HANDLE go = CreateEvent(NULL, TRUE, FALSE, NULL);
	UINT64 *all = malloc((size_t) nthreads * (size_t) rounds * sizeof *all);
	UINT64 longest = 0, total_halts = 0;
	int i, j, halts = 0, failures = 0, wrong = 0, vcpus_touched = 0, threads_on_several = 0;
	unsigned char any_used[256];

	if (!ts || !hs || !all || FAILED(make_partition(&p, (UINT32) k))
	    || FAILED(WHvMapGpaRange(p, ctl, 0, CONTROL,
	                             WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite | WHvMapGpaRangeFlagExecute))) {
		emit(pk(pfx, "setup"), "0"); return;
	}
	for (i = 0; i < k; i++)
		if (FAILED(WHvCreateVirtualProcessor(p, (UINT32) i, 0)) || FAILED(enter(p, (UINT32) i))) { emit(pk(pfx, "setup"), "0"); return; }
	memset(&pl, 0, sizeof pl);
	pl.p = p; pl.k = k; pl.free_stack = calloc((size_t) (k > 0 ? k : 1), sizeof(int));
	for (i = 0; i < k; i++) pl.free_stack[pl.top++] = i;
	pl.sem = CreateSemaphore(NULL, k, k, NULL);
	InitializeCriticalSection(&pl.cs);
	emit(pk(pfx, "setup"), "1");
	emit(pk(pfx, "pool_vcpus"), "%d", k);
	emit(pk(pfx, "threads"), "%d", nthreads);
	emit(pk(pfx, "rounds_per_thread"), "%d", rounds);

	for (i = 0; i < nthreads; i++) {
		ts[i].pl = &pl; ts[i].rounds = rounds; ts[i].go = go;
		ts[i].counter = (UINT64) i * 1000000ULL;   /* distinct starting values */
		ts[i].lat = all + (size_t) i * (size_t) rounds;
		hs[i] = CreateThread(NULL, 0, pool_thread_main, &ts[i], 0, NULL);
	}
	Sleep(50);
	SetEvent(go);
	for (i = 0; i < nthreads; i++) {
		if (WaitForSingleObject(hs[i], 120000) != WAIT_OBJECT_0) { emit(pk(pfx, "pool"), "hung"); return; }
		CloseHandle(hs[i]);
	}
	memset(any_used, 0, sizeof any_used);
	for (i = 0; i < nthreads; i++) {
		int mine = 0;
		halts += ts[i].halts; failures += ts[i].failures; wrong += ts[i].wrong;
		if (ts[i].total_ns > longest) longest = ts[i].total_ns;
		if (ts[i].counter != (UINT64) i * 1000000ULL + (UINT64) ts[i].halts) wrong++;
		for (j = 0; j < 256; j++) { if (ts[i].used[j]) { any_used[j] = 1; mine++; } }
		if (mine > 1) threads_on_several++;
	}
	for (j = 0; j < 256; j++) vcpus_touched += any_used[j];
	total_halts = (UINT64) halts;
	emit(pk(pfx, "pool"), "%s", (failures == 0 && wrong == 0 && halts == nthreads * rounds) ? "isolates" : "failed");
	emit(pk(pfx, "halts"), "%d", halts);
	emit(pk(pfx, "run_failures"), "%d", failures);
	emit(pk(pfx, "counters_wrong"), "%d", wrong);
	emit(pk(pfx, "vcpus_touched"), "%d", vcpus_touched);
	emit(pk(pfx, "threads_on_several_vcpus"), "%d", threads_on_several);
	/* the compacted latencies: every completed round, all threads */
	{
		size_t n = 0;
		for (i = 0; i < nthreads; i++)
			for (j = 0; j < ts[i].halts && j < rounds; j++) all[n++] = ts[i].lat[j];
		/* lat is dense from 0 to halts-1 only when no round failed; close enough for the stats */
		stats(pk(pfx, "round"), all, n ? n : 1);
	}
	emit(pk(pfx, "aggregate_rounds_per_second"), "%llu",
	     longest ? (unsigned long long) (total_halts * 1000000000ULL / longest) : 0ULL);
	emit(pk(pfx, "wall_ns"), "%llu", (unsigned long long) longest);
	for (i = 0; i < k; i++) WHvDeleteVirtualProcessor(p, (UINT32) i);
	WHvUnmapGpaRange(p, 0, CONTROL);
	WHvDeletePartition(p);
	DeleteCriticalSection(&pl.cs);
	CloseHandle(pl.sem); CloseHandle(go);
	free(pl.free_stack); free(ts); free(hs); free(all);
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	SYSTEM_INFO si;
	UINT32 w = 0;
	UINT8 *ctl;
	int i, present, part_cap = 512, vcpu_cap = 256, nvcpu, exits = 5000, pool_threads = 64, pool_rounds = 2000;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--hold")) {
			/* q3's second process: hold one partition with a vCPU, say so,
			 * and leave. The parent is holding its own the whole time. */
			WHV_PARTITION_HANDLE p = NULL;
			HRESULT hr = make_partition(&p, 1);
			if (SUCCEEDED(hr)) hr = WHvCreateVirtualProcessor(p, 0, 0);
			if (SUCCEEDED(hr)) printf("held\n"); else printf("refused:%s:0x%08lx\n", make_stage, (unsigned long) hr);
			fflush(stdout);
			Sleep(200);
			if (p) WHvDeletePartition(p);
			return SUCCEEDED(hr) ? 0 : 1;
		}
		if (!strcmp(argv[i], "--hold-shared") && i + 1 < argc) {
			/* q3c's second process: open the parent's section, map a view of
			 * it into a partition of our own, and say what the parent wrote. */
			WHV_PARTITION_HANDLE p = NULL;
			HANDLE sec = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[i + 1]);
			UINT8 *v = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
			HRESULT hr = v ? make_partition(&p, 1) : E_FAIL;
			if (SUCCEEDED(hr)) hr = WHvMapGpaRange(p, v, 0, 64 * 1024, WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);
			if (SUCCEEDED(hr))
				printf("mapped,sees-parent-write:%d\n", *(UINT64 *) (v + 0x100) == 0x5348415245ULL);
			else
				printf("refused:0x%08lx\n", (unsigned long) hr);
			fflush(stdout);
			Sleep(200);
			if (p) WHvDeletePartition(p);
			return SUCCEEDED(hr) ? 0 : 1;
		}
		if (!strcmp(argv[i], "--partitions") && i + 1 < argc) { part_cap = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--vcpus") && i + 1 < argc) { vcpu_cap = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--exits") && i + 1 < argc) { exits = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--pool-threads") && i + 1 < argc) { pool_threads = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--pool-rounds") && i + 1 < argc) { pool_rounds = atoi(argv[++i]); continue; }
		fprintf(stderr, "partition-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);
	GetSystemInfo(&si);
	nvcpu = (int) si.dwNumberOfProcessors;
	if (nvcpu > 8) nvcpu = 8;
	if (nvcpu < 2) nvcpu = 2;

	memset(&cap, 0, sizeof cap);
	present = SUCCEEDED(WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &w))
	          && cap.HypervisorPresent;
	emit("hypervisor_present", "%d", present);
	emit("host_processors", "%lu", (unsigned long) si.dwNumberOfProcessors);
	if (!present) return 0;

	ctl = VirtualAlloc(NULL, CONTROL, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!ctl) return 1;
	build_control(ctl);

	q1_create_setup();
	q2_first_run(ctl);
	q3_partitions_per_process(part_cap);
	q4_vcpus_per_partition(ctl, vcpu_cap);
	q5_thread_handoff(ctl);
	q6_concurrent_exits(ctl, nvcpu, exits);
	q7_vcpu_pool("q7", ctl, nvcpu, nvcpu, pool_rounds);          /* one thread per vCPU: the switch cost alone */
	q7_vcpu_pool("q7b", ctl, nvcpu, pool_threads, pool_rounds);  /* the pool oversubscribed */
	trace("done");
	return 0;
}
