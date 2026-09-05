/* Can a running WHP virtual processor be forced out of WHvRunVirtualProcessor
 * from a second host thread, and is the interruption a resumable one rather
 * than a teardown?
 *
 * This is the H-side twin of spike (d)'s NT hijack. Proposal 0011 section 3
 * names one substrate call thread_interrupt(tid) -- "force it into the kernel,
 * soon" -- and section 7 says that under H it is WHvCancelRunVirtualProcessor,
 * with the vCPU's exit as the moment a signal is delivered. Nothing here had
 * ever called it; spike (f) built the partition and priced an exit but left the
 * cancel untouched, calling it out as the gap. This spike is that gap.
 *
 * The guest is spike (f)'s: a handful of bytes of machine code, four page-table
 * pages, a GDT and a TSS in one host allocation mapped as guest physical
 * memory. What is new is a never-exiting ring-3 loop, a second NT thread that
 * owns WHvRunVirtualProcessor, and a cancelling thread that plays the kernel.
 *
 * Six answers on stdout as key=value, then a reading, then a verdict with a
 * deterministic finding word. Everything here is Win32; there is no POSIX.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhvplatform.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE "whp-vcpu-interrupt 1.0"

/* Guest physical layout, spike (f)'s with two additions: a stub the host
 * injects a jump to, and a scratch word the stub writes so the host can prove
 * from outside that it ran. The first page stays zero. */
#define GPA_PML4    0x1000
#define GPA_PDPT    0x2000
#define GPA_PD      0x3000
#define GPA_GDT     0x4000
#define GPA_TSS     0x5000
#define GPA_CODE    0x6000
#define GPA_STACK   0x8000            /* top of the ring-3 stack, grows down */
#define GPA_SCRATCH 0xa000            /* the stub writes MAGIC here */
#define MAPPED      0x00400000        /* four megabytes are backed */

#define EP_LOOP     (GPA_CODE + 0x000)
#define EP_STUB     (GPA_CODE + 0x100)

#define STUB_MAGIC  0xc0de5347UL      /* what the injected stub stores */

static int verbose = 0;

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

static UINT8 *mem;                    /* the host allocation backing GPA 0 */
static WHV_PARTITION_HANDLE part;

static void put64(UINT64 gpa, UINT64 v) { memcpy(mem + gpa, &v, 8); }
static void put_code(UINT64 gpa, const UINT8 *b, size_t n) { memcpy(mem + gpa, b, n); }

/* Identity page tables, spike (f)'s: one PML4 and one PDPT entry, eight
 * two-megabyte pages, every level user and writable. */
static void build_page_tables(void)
{
	int i;
	put64(GPA_PML4, GPA_PDPT | 0x7);
	put64(GPA_PDPT, GPA_PD | 0x7);
	for (i = 0; i < 8; i++)
		put64(GPA_PD + 8 * i, ((UINT64) i << 21) | 0x87);
}

/* The GDT spike (f) laid out for STAR; unchanged, though no syscall is taken
 * here. The ring-3 selectors are what enter_long_mode loads. */
static void build_gdt(void)
{
	put64(GPA_GDT + 0x00, 0);
	put64(GPA_GDT + 0x08, 0x00af9b000000ffffULL);   /* ring-0 code, long */
	put64(GPA_GDT + 0x10, 0x00cf93000000ffffULL);   /* ring-0 data */
	put64(GPA_GDT + 0x18, 0x00cffb000000ffffULL);   /* ring-3 code, 32-bit */
	put64(GPA_GDT + 0x20, 0x00cff3000000ffffULL);   /* ring-3 data, 0x23 */
	put64(GPA_GDT + 0x28, 0x00affb000000ffffULL);   /* ring-3 code, long, 0x2b */
	put64(GPA_GDT + 0x30, 0x0000890000000067ULL | ((UINT64)(GPA_TSS & 0xffffff) << 16)
	                      | ((UINT64)((GPA_TSS >> 24) & 0xff) << 56));
	put64(GPA_GDT + 0x38, 0);
	put64(GPA_TSS + 0x04, GPA_STACK);
}

/* The thing under test needs a guest that never leaves on its own: no hlt (it
 * is privileged at CPL 3 and would triple-fault without an IDT), no syscall, no
 * fault. A tight increment loop is exactly that. rax climbs so the host can
 * prove afterwards that the guest resumed where it stopped rather than being
 * torn down and rebuilt. */
static const UINT8 code_loop[] = {
	0x48, 0xff, 0xc0,                               /* inc %rax */
	0xeb, 0xfb                                      /* jmp .-5 (back to inc) */
};

/* The injected handler stub, section 7's "resumes in the signal trampoline
 * with the frame built" reduced to its observable core: store a known value
 * where the host can read it, then spin so the host can cancel it in turn. It
 * is entered by the host rewriting rip and rsp on a cancel, the register-
 * injection this question is about. */
static const UINT8 code_stub[] = {
	0x48, 0xb8, 0x47, 0x53, 0xde, 0xc0, 0x00, 0x00, 0x00, 0x00,  /* movabs $MAGIC, %rax */
	0x48, 0xbb, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* movabs $SCRATCH, %rbx */
	0x48, 0x89, 0x03,                               /* mov %rax, (%rbx) */
	0xeb, 0xfe                                      /* jmp .-0 (spin here) */
};
#define STUB_SPIN (EP_STUB + 23)      /* rip settles on the jmp once stored */

static HRESULT setreg(const WHV_REGISTER_NAME *n, const WHV_REGISTER_VALUE *v, UINT32 c)
{
	return WHvSetVirtualProcessorRegisters(part, 0, n, c, v);
}

static HRESULT getreg(const WHV_REGISTER_NAME *n, WHV_REGISTER_VALUE *v, UINT32 c)
{
	return WHvGetVirtualProcessorRegisters(part, 0, n, c, v);
}

static UINT64 reg64(WHV_REGISTER_NAME name)
{
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	if (FAILED(getreg(&name, &v, 1)))
		return 0;
	return v.Reg64;
}

static HRESULT set64(WHV_REGISTER_NAME name, UINT64 val)
{
	WHV_REGISTER_VALUE v;
	memset(&v, 0, sizeof v);
	v.Reg64 = val;
	return setreg(&name, &v, 1);
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

/* Long mode at CPL 3 in one call, spike (f)'s enter, with rip and the initial
 * rax passed in so a fresh run always starts from a known state. */
static HRESULT enter_long_mode(UINT64 rip, UINT64 rax)
{
	WHV_REGISTER_NAME n[24];
	WHV_REGISTER_VALUE v[24];
	WHV_X64_SEGMENT_REGISTER data = seg(0x23, 3, 3, 0, 1);
	WHV_X64_SEGMENT_REGISTER code = seg(0x2b, 0xb, 3, 1, 0);
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

	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterTr;
	v[i].Segment.Base = GPA_TSS;
	v[i].Segment.Limit = 0x67;
	v[i].Segment.Selector = 0x30;
	v[i].Segment.SegmentType = 11;
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

	memset(&v[i], 0, sizeof v[i]);
	n[i] = WHvX64RegisterIdtr;
	i++;

	n[i] = WHvX64RegisterRflags; v[i++].Reg64 = 0x2;
	n[i] = WHvX64RegisterRsp;    v[i++].Reg64 = GPA_STACK - 0x10;
	n[i] = WHvX64RegisterRip;    v[i++].Reg64 = rip;
	n[i] = WHvX64RegisterRax;    v[i++].Reg64 = rax;
	return setreg(n, v, (UINT32) i);
}

/* One NT thread owns WHvRunVirtualProcessor, as substrate H's design has it:
 * "one virtual processor per Linux thread, each driven by one NT thread that
 * loops on WHvRunVirtualProcessor." Everything else -- register reads and
 * writes, the cancel -- happens on the main thread, which is legal only while
 * the vCPU is not inside a run. The handshake below keeps that true. */
static HANDLE ev_go;                  /* main -> runner: enter a run now */
static HANDLE ev_ran;                 /* runner -> main: the run returned */
static volatile LONG in_run;          /* runner is inside WHvRunVirtualProcessor */
static volatile LONG stop;            /* tear the runner down */
static volatile LONG64 t_return;      /* QPC the instant a run returned */
static volatile LONG last_reason;     /* the exit reason of the last run */
static volatile LONG last_failed;     /* the run call itself failed */
static double qpc_ns;                 /* nanoseconds per QPC tick */

static LONG64 qpc(void)
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

static DWORD WINAPI runner(LPVOID arg)
{
	WHV_RUN_VP_EXIT_CONTEXT ex;
	HRESULT hr;
	(void) arg;
	for (;;) {
		WaitForSingleObject(ev_go, INFINITE);
		if (stop)
			return 0;
		memset(&ex, 0, sizeof ex);
		InterlockedExchange(&in_run, 1);
		hr = WHvRunVirtualProcessor(part, 0, &ex, sizeof ex);
		t_return = qpc();
		InterlockedExchange(&in_run, 0);
		last_failed = FAILED(hr);
		last_reason = (LONG) ex.ExitReason;
		SetEvent(ev_ran);
	}
}

/* Spin until the runner is demonstrably inside a run. There is a hair of a
 * window between in_run going to 1 and the run call actually executing guest
 * code; the busy wait past it is what makes q2 time an interrupt of a running
 * vCPU rather than a race with entry. */
static void wait_in_run(void)
{
	while (!in_run)
		YieldProcessor();
}

static void spin_ns(double ns)
{
	LONG64 start = qpc();
	double ticks = ns / qpc_ns;
	while ((double) (qpc() - start) < ticks)
		YieldProcessor();
}

static void cancel(void)
{
	WHvCancelRunVirtualProcessor(part, 0, 0);
}

/* Cancel a vCPU that is already running. Precondition: the runner is idle and
 * the registers are set for the run to take. Records, through *t0, the QPC just
 * before the cancel, so the caller can pair it with t_return. */
static int run_then_cancel(double spin_before_ns, LONG64 *t0)
{
	SetEvent(ev_go);
	wait_in_run();
	if (spin_before_ns > 0)
		spin_ns(spin_before_ns);
	if (t0)
		*t0 = qpc();
	cancel();
	if (WaitForSingleObject(ev_ran, 5000) != WAIT_OBJECT_0) {
		trace("run_then_cancel: runner did not return");
		return -1;
	}
	return 0;
}

/* A running vCPU is not yet a vCPU executing guest code: WHP's VM entry takes a
 * few microseconds, spike (f)'s exit figure, and a cancel that lands inside it
 * exits the guest at its first instruction. The running-vCPU questions spin
 * well past entry before they cancel, so what they interrupt is guest code. */
#define RUN_SPIN_NS 60000.0

/* q4 needs the stub to actually run, not merely be entered, so it polls the
 * shared scratch word the stub stores into -- reading host memory while the
 * guest runs is safe, it is only memory -- and cancels once it appears. */
static int run_until_written(UINT32 want, int timeout_ms)
{
	LONG64 start;
	double limit;
	SetEvent(ev_go);
	start = qpc();
	limit = (double) timeout_ms * 1000000.0 / qpc_ns;
	while (*(volatile UINT32 *) (mem + GPA_SCRATCH) != want) {
		if ((double) (qpc() - start) > limit)
			break;
		YieldProcessor();
	}
	cancel();
	return WaitForSingleObject(ev_ran, 5000) == WAIT_OBJECT_0;
}

static int cmp_u64(const void *a, const void *b)
{
	UINT64 x = *(const UINT64 *) a, y = *(const UINT64 *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

/* Idle the runner without any guest run: used before a cancel-before-run trial,
 * where the point is that the vCPU is provably not in a run when the cancel is
 * issued. It already is between runs, so this only asserts it. */
static int runner_idle(void) { return !in_run; }

static const char *reason_word(LONG r)
{
	switch (r) {
	case WHvRunVpExitReasonCanceled:      return "canceled";
	case WHvRunVpExitReasonX64Halt:       return "halt";
	case WHvRunVpExitReasonMemoryAccess:  return "memory-access";
	case WHvRunVpExitReasonNone:          return "none";
	default:                              return "other";
	}
}

/* The six questions, each producing one raw value line, a pass bit, and a line
 * of reading. They are filled in order and printed together at the end, so the
 * transcript's shape never depends on how far the run got. */
static const char *qkey[6] = {
	"q1_cancel_returns_running_vcpu",
	"q2_latency_ns",
	"q3_resumable",
	"q4_deliver_then_resume",
	"q5_cancel_before_run",
	"q6_pending_vs_delivered"
};
static const char *qread[6] = {
	"a running vCPU is forced out by a cancel from another thread",
	"latency from the cancel call to the vCPU thread seeing the return",
	"after a cancel the vCPU re-enters and the guest resumes where it stopped",
	"on the cancel the host injects a handler stub, it runs, then the loop resumes",
	"a cancel issued while the vCPU is not inside a run",
	"every cancel over many iterations yields exactly one canceled exit"
};
static char qval[6][256];
static int qpass[6];

static void set_all_unavailable(void)
{
	int i;
	for (i = 0; i < 6; i++) {
		strcpy(qval[i], "unavailable");
		qpass[i] = 0;
	}
}

static void report(const char *finding, const char *verdict)
{
	int i;
	printf("reading, question by question\n\n");
	for (i = 0; i < 6; i++)
		printf("  q%d %s -- %s [%s]\n", i + 1, qread[i], qval[i],
		       qpass[i] ? "pass" : "fail");
	printf("\nraw\n\n");
	for (i = 0; i < 6; i++)
		printf("    %s=%s\n", qkey[i], qval[i]);
	printf("\nverdict\n\n");
	printf("    shape=");
	for (i = 0; i < 6; i++)
		printf("%s%s:%s", i ? "," : "", qkey[i], qpass[i] ? "pass" : "fail");
	printf("\n    verdict=%s\n", verdict);
	printf("    finding=%s\n", finding);
	printf("    probe=%s\n", RELEASE);
}

int main(int argc, char **argv)
{
	WHV_CAPABILITY cap;
	WHV_PARTITION_PROPERTY prop;
	LARGE_INTEGER freq;
	HANDLE th;
	HRESULT hr;
	UINT32 written = 0;
	UINT64 latency_n = 2000, races_n = 10000;
	int present, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--latency") && i + 1 < argc) {
			latency_n = strtoull(argv[++i], NULL, 0); continue;
		}
		if (!strcmp(argv[i], "--races") && i + 1 < argc) {
			races_n = strtoull(argv[++i], NULL, 0); continue;
		}
		fprintf(stderr, "whp-interrupt: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);
	qpc_ns = 1000000000.0 / (double) freq.QuadPart;

	/* The hypervisor and the partition, spike (f)'s preflight. Any failure
	 * here is not this spike's question -- it is spike (f)'s -- so it collapses
	 * to a single honest word and stops. */
	memset(&cap, 0, sizeof cap);
	hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof cap, &written);
	present = SUCCEEDED(hr) && cap.HypervisorPresent;
	if (!present) {
		set_all_unavailable();
		report("whp-unavailable", "no");
		return 0;
	}
	hr = WHvCreatePartition(&part);
	if (SUCCEEDED(hr)) {
		memset(&prop, 0, sizeof prop);
		prop.ProcessorCount = 1;
		hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeProcessorCount,
		                             &prop, sizeof prop);
	}
	if (SUCCEEDED(hr))
		hr = WHvSetupPartition(part);
	if (FAILED(hr)) {
		trace("partition setup: 0x%08lx", (unsigned long) hr);
		set_all_unavailable();
		report("whp-unavailable", "no");
		return 0;
	}

	mem = VirtualAlloc(NULL, MAPPED, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	hr = mem ? WHvMapGpaRange(part, mem, 0, MAPPED,
	                          WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite
	                          | WHvMapGpaRangeFlagExecute)
	         : E_OUTOFMEMORY;
	if (FAILED(hr)) {
		set_all_unavailable();
		report("whp-unavailable", "no");
		return 0;
	}
	memset(mem, 0, MAPPED);
	build_page_tables();
	build_gdt();
	put_code(EP_LOOP, code_loop, sizeof code_loop);
	put_code(EP_STUB, code_stub, sizeof code_stub);

	hr = WHvCreateVirtualProcessor(part, 0, 0);
	if (SUCCEEDED(hr))
		hr = enter_long_mode(EP_LOOP, 0);
	if (FAILED(hr)) {
		trace("vcpu/registers: 0x%08lx", (unsigned long) hr);
		set_all_unavailable();
		report("whp-unavailable", "no");
		return 0;
	}

	ev_go = CreateEventW(NULL, FALSE, FALSE, NULL);
	ev_ran = CreateEventW(NULL, FALSE, FALSE, NULL);
	th = CreateThread(NULL, 0, runner, NULL, 0, NULL);
	if (!ev_go || !ev_ran || !th) {
		set_all_unavailable();
		report("whp-unavailable", "no");
		return 0;
	}

	/* q1. The bare fact the whole substrate call rests on: a vCPU spinning in
	 * a guest loop that never exits on its own is forced out by a cancel from
	 * the main thread, and the exit reason is Canceled. A couple of hundred
	 * confirm it is the rule, not a fluke. */
	{
		int runs = 200, canceled = 0, other = 0, failed = 0;
		UINT16 cs = 0;
		enter_long_mode(EP_LOOP, 0);
		for (i = 0; i < runs; i++) {
			if (run_then_cancel(RUN_SPIN_NS, NULL) != 0) { failed++; break; }
			if (last_failed) { failed++; break; }
			if (last_reason == WHvRunVpExitReasonCanceled) canceled++;
			else other++;
			if (i == 0) cs = cs_selector();
		}
		qpass[0] = (canceled == runs) && (other == 0) && (failed == 0);
		snprintf(qval[0], sizeof qval[0],
		         "%s,exit-reason:0x%x,cpl:%u,runs:%d,other:%d,failed:%d",
		         qpass[0] ? "canceled" : reason_word(last_reason),
		         (unsigned) WHvRunVpExitReasonCanceled, (unsigned)(cs & 3),
		         runs, other, failed);
	}

	/* q2. How long from the cancelling thread's call to the vCPU thread seeing
	 * the run return. A measurement, not a finding: the word is "measured" and
	 * the numbers ride along, to be read against spike (d)'s ~18 us hijack and
	 * spike (f)'s ~5 us exit. */
	{
		UINT64 *lat = malloc((size_t) latency_n * sizeof *lat);
		UINT64 good = 0, notcxl = 0;
		LONG64 t0;
		for (i = 0; lat && (UINT64) i < latency_n; i++) {
			if (run_then_cancel(RUN_SPIN_NS, &t0) != 0)
				break;
			if (last_reason != WHvRunVpExitReasonCanceled)
				notcxl++;
			lat[good++] = (UINT64) ((double) (t_return - t0) * qpc_ns);
		}
		if (good > 0) {
			qsort(lat, (size_t) good, sizeof *lat, cmp_u64);
			qpass[1] = (notcxl == 0);
			snprintf(qval[1], sizeof qval[1],
			         "measured,median_ns:%llu,p99_ns:%llu,min_ns:%llu,samples:%llu,non-canceled:%llu",
			         (unsigned long long) lat[good / 2],
			         (unsigned long long) lat[(good * 99) / 100],
			         (unsigned long long) lat[0],
			         (unsigned long long) good, (unsigned long long) notcxl);
		} else {
			qpass[1] = 0;
			strcpy(qval[1], "not-measured,samples:0");
		}
		free(lat);
	}

	/* q3. What makes a cancel an interrupt rather than a teardown: after the
	 * exit, the same vCPU is re-entered with no register rewrite and the guest
	 * carries on from where it was stopped. rax must have climbed and rip must
	 * still be inside the two-instruction loop, both times. */
	{
		UINT64 rax0, rax1, rip0, rip1;
		enter_long_mode(EP_LOOP, 0);
		run_then_cancel(RUN_SPIN_NS, NULL);
		rax0 = reg64(WHvX64RegisterRax);
		rip0 = reg64(WHvX64RegisterRip);
		run_then_cancel(RUN_SPIN_NS, NULL);     /* re-enter, no rewrite */
		rax1 = reg64(WHvX64RegisterRax);
		rip1 = reg64(WHvX64RegisterRip);
		{
			int in0 = rip0 >= EP_LOOP && rip0 <= EP_LOOP + sizeof code_loop;
			int in1 = rip1 >= EP_LOOP && rip1 <= EP_LOOP + sizeof code_loop;
			qpass[2] = in0 && in1 && (rax1 > rax0);
			snprintf(qval[2], sizeof qval[2],
			         "%s,rip-in-loop,rax0:0x%llx,rax1:0x%llx,rip:0x%llx",
			         qpass[2] ? "resumes" : "no-resume",
			         (unsigned long long) rax0, (unsigned long long) rax1,
			         (unsigned long long) rip1);
		}
	}

	/* q4. The real use modelled: on the cancel the host injects a handler by
	 * rewriting rip to a stub and rsp 128 below the interrupted value, the
	 * red-zone gap DR-0030 fixed on the N side; the stub runs and stores a
	 * known word the host reads back; then the saved context is restored and
	 * the original loop resumes. This is delivery-by-register-injection, the H
	 * analogue of spike (d)'s context rewrite. */
	{
		UINT64 rip_s, rsp_s, rax_s, rsp_stub, rax_r, rip_r, magic;
		int stub_ran, rsp_adj, resumed;
		enter_long_mode(EP_LOOP, 0);
		*(volatile UINT32 *) (mem + GPA_SCRATCH) = 0;
		run_then_cancel(RUN_SPIN_NS, NULL);
		rip_s = reg64(WHvX64RegisterRip);
		rsp_s = reg64(WHvX64RegisterRsp);
		rax_s = reg64(WHvX64RegisterRax);

		set64(WHvX64RegisterRip, EP_STUB);
		set64(WHvX64RegisterRsp, rsp_s - 128);
		run_until_written(STUB_MAGIC, 1000);    /* the stub runs, then spins */
		magic = *(volatile UINT32 *) (mem + GPA_SCRATCH);
		rsp_stub = reg64(WHvX64RegisterRsp);
		stub_ran = (magic == STUB_MAGIC);
		rsp_adj = (rsp_stub == rsp_s - 128);

		set64(WHvX64RegisterRip, rip_s);
		set64(WHvX64RegisterRsp, rsp_s);
		set64(WHvX64RegisterRax, rax_s);
		run_then_cancel(RUN_SPIN_NS, NULL);     /* the loop resumes */
		rax_r = reg64(WHvX64RegisterRax);
		rip_r = reg64(WHvX64RegisterRip);
		resumed = (rax_r > rax_s) &&
		          (rip_r >= EP_LOOP && rip_r <= EP_LOOP + sizeof code_loop);
		qpass[3] = stub_ran && rsp_adj && resumed;
		snprintf(qval[3], sizeof qval[3],
		         "%s,stub-ran:%d,rsp-adjusted:%d,restored:%d,magic:0x%llx",
		         qpass[3] ? "delivers-and-resumes" : "no-delivery",
		         stub_ran, rsp_adj, resumed, (unsigned long long) magic);
	}

	/* q5. The in-kernel-flag concern spike (d) handled by hand on the N side: a
	 * cancel issued while the vCPU is not inside a run. Is it latched, so the
	 * next run returns Canceled at once, or lost? The runner is idle between
	 * runs, so the cancel here provably precedes the run it should stop. A watch
	 * of a quarter second tells a latched cancel from a lost one, since the loop
	 * would otherwise never return. */
	{
		int trials = 200, latched = 0, lost = 0;
		UINT64 prog_max = 0;
		for (i = 0; i < trials; i++) {
			enter_long_mode(EP_LOOP, 0);        /* runner idle: safe to set */
			if (!runner_idle()) { lost = trials; break; }
			cancel();                           /* before the run */
			SetEvent(ev_go);
			if (WaitForSingleObject(ev_ran, 250) == WAIT_OBJECT_0) {
				if (last_reason == WHvRunVpExitReasonCanceled) latched++;
				{
					UINT64 p = reg64(WHvX64RegisterRax);
					if (p > prog_max) prog_max = p;
				}
			} else {
				lost++;
				cancel();
				WaitForSingleObject(ev_ran, INFINITE);
			}
		}
		qpass[4] = (latched == trials) && (lost == 0);
		snprintf(qval[4], sizeof qval[4],
		         "%s,next-run-canceled,trials:%d,latched:%d,lost:%d,progress-max:0x%llx",
		         qpass[4] ? "latched" : (lost ? "lost" : "partial"),
		         trials, latched, lost, (unsigned long long) prog_max);
	}

	/* q6. The race run at volume: a cancel fired the instant the run is asked
	 * for, ten thousand times, sometimes landing before the run and sometimes
	 * inside it. Every cancel must produce exactly one Canceled exit -- none
	 * lost to a hang, none returning some other reason. */
	{
		UINT64 canceled = 0, other = 0, lost = 0, k;
		enter_long_mode(EP_LOOP, 0);
		for (k = 0; k < races_n; k++) {
			SetEvent(ev_go);
			cancel();                           /* natural race with entry */
			if (WaitForSingleObject(ev_ran, 250) == WAIT_OBJECT_0) {
				if (last_reason == WHvRunVpExitReasonCanceled) canceled++;
				else other++;
			} else {
				lost++;
				cancel();
				WaitForSingleObject(ev_ran, INFINITE);
				if (last_reason == WHvRunVpExitReasonCanceled) canceled++;
				else other++;
			}
		}
		qpass[5] = (canceled == races_n) && (other == 0) && (lost == 0);
		snprintf(qval[5], sizeof qval[5],
		         "%s,cancels:%llu,canceled:%llu,other:%llu,lost:%llu",
		         qpass[5] ? "one-to-one" : "not-one-to-one",
		         (unsigned long long) races_n, (unsigned long long) canceled,
		         (unsigned long long) other, (unsigned long long) lost);
	}

	/* Tear the runner down cleanly before reporting. */
	stop = 1;
	SetEvent(ev_go);
	WaitForSingleObject(th, 2000);

	/* The finding names the weakest of the properties that make H's
	 * thread_interrupt the twin of N's: it must return a running vCPU, resume
	 * it, deliver by register injection, and latch a cancel that beats the run.
	 * Each failure has its own word so the verdict says which property gave. */
	{
		const char *finding, *verdict;
		int all = qpass[0] && qpass[1] && qpass[2] && qpass[3] && qpass[4] && qpass[5];
		if (!qpass[0])
			finding = "cancel-no-exit";
		else if (!qpass[2])
			finding = "interrupt-teardown-only";
		else if (!qpass[3])
			finding = "interrupt-resumable-no-delivery";
		else if (!qpass[4])
			finding = "cancel-not-latched";
		else
			finding = "interrupt-delivers-resumable";
		verdict = all ? "yes" : "no";
		report(finding, verdict);
	}

	WHvDeleteVirtualProcessor(part, 0);
	WHvUnmapGpaRange(part, 0, MAPPED);
	WHvDeletePartition(part);
	return 0;
}
