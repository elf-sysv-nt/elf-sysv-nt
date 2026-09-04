/*
 * hijack -- can a signal be delivered into a thread that is not cooperating,
 * on this Windows 11 build, by suspending it and rewriting its context?
 *
 * Proposal 0011 delivers a signal to a thread running user code with nothing
 * pending by interrupting it: NtSuspendThread, NtGetContextThread, a check that
 * the thread is in user code rather than the kernel, a rewrite of %rip to the
 * trampoline and %rsp to a frame built below the red zone, NtSetContextThread,
 * NtResumeThread. That is the substrate's thread_interrupt, and nothing this
 * project has run has ever measured it here. This probe measures it, seven ways.
 *
 * It is the NT-side sibling of spike/redzone-delivery, which drove the same
 * hijack to price the red-zone reservation DR-0030 and DR-0050 rest on. That
 * one asked what a delivery does to the frame; this one asks whether the
 * delivery reaches a non-cooperating thread at all -- one spinning in user mode,
 * and one parked in a kernel wait -- and whether the in-kernel flag the design
 * leans on actually describes where a suspended thread stopped.
 *
 * Built and driven by measure.sh. See README.md for the mechanism and cases.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_VERSION "hijack 1.0"

/* The block shared with the asm targets. Every offset is written again in
 * hijack.S; the static assertions below hold the two copies together. */
struct hij {
	volatile uint64_t stop;
	volatile uint64_t running;
	uint64_t rsp;		/* the %rsp a target recorded */
	uint64_t iters;		/* spin_target scan passes */
	uint64_t depth;		/* bytes below %rsp painted, multiple of 8 */
	uint64_t failures;	/* words found changed */
	uint64_t first_bad;
	uint64_t shallowest;	/* nearest offset below %rsp that moved */
	uint64_t deepest;
	uint64_t got;
	volatile uint64_t in_kernel;	/* flag_target's gate flag */
	uint64_t crit_lo;	/* flag_target's critical region, %rip bounds */
	uint64_t crit_hi;
	uint64_t flagiters;
};

_Static_assert(offsetof(struct hij, stop) == 0, "stop");
_Static_assert(offsetof(struct hij, running) == 8, "running");
_Static_assert(offsetof(struct hij, rsp) == 16, "rsp");
_Static_assert(offsetof(struct hij, iters) == 24, "iters");
_Static_assert(offsetof(struct hij, depth) == 32, "depth");
_Static_assert(offsetof(struct hij, failures) == 40, "failures");
_Static_assert(offsetof(struct hij, first_bad) == 48, "first_bad");
_Static_assert(offsetof(struct hij, shallowest) == 56, "shallowest");
_Static_assert(offsetof(struct hij, deepest) == 64, "deepest");
_Static_assert(offsetof(struct hij, got) == 72, "got");
_Static_assert(offsetof(struct hij, in_kernel) == 80, "in_kernel");
_Static_assert(offsetof(struct hij, crit_lo) == 88, "crit_lo");
_Static_assert(offsetof(struct hij, crit_hi) == 96, "crit_hi");
_Static_assert(offsetof(struct hij, flagiters) == 104, "flagiters");

extern void spin_target(struct hij *w);
extern void flag_target(struct hij *w);
extern void deliver_stub(void);
extern volatile uint64_t handler_calls;
extern volatile uint64_t handler_done;
extern volatile uint64_t t_handler;

/* ---- the NT calls, resolved from ntdll ---------------------------------- */

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *susp_t)(HANDLE, PULONG);
typedef NTSTATUS (NTAPI *resm_t)(HANDLE, PULONG);
typedef NTSTATUS (NTAPI *getc_t)(HANDLE, PCONTEXT);
typedef NTSTATUS (NTAPI *setc_t)(HANDLE, PCONTEXT);
typedef NTSTATUS (NTAPI *wait_t)(HANDLE, BOOLEAN, PLARGE_INTEGER);
typedef NTSTATUS (NTAPI *dely_t)(BOOLEAN, PLARGE_INTEGER);
typedef NTSTATUS (NTAPI *apc_t)(HANDLE, PVOID, PVOID, PVOID, PVOID);

static susp_t NtSuspend;
static resm_t NtResume;
static getc_t NtGetCtx;
static setc_t NtSetCtx;
static wait_t NtWait;
static dely_t NtDelay;
static apc_t  NtApc;

static void resolve_ntdll(void)
{
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");

	NtSuspend = (susp_t)(void *)GetProcAddress(nt, "NtSuspendThread");
	NtResume  = (resm_t)(void *)GetProcAddress(nt, "NtResumeThread");
	NtGetCtx  = (getc_t)(void *)GetProcAddress(nt, "NtGetContextThread");
	NtSetCtx  = (setc_t)(void *)GetProcAddress(nt, "NtSetContextThread");
	NtWait    = (wait_t)(void *)GetProcAddress(nt, "NtWaitForSingleObject");
	NtDelay   = (dely_t)(void *)GetProcAddress(nt, "NtDelayExecution");
	NtApc     = (apc_t)(void *)GetProcAddress(nt, "NtQueueApcThread");
}

static int ntdll_ok(void)
{
	return NtSuspend && NtResume && NtGetCtx && NtSetCtx &&
	       NtWait && NtDelay && NtApc;
}

/* ---- outcomes ----------------------------------------------------------- */

struct outcome {
	const char *key;	/* the raw key=value name */
	const char *reading;	/* one prose line, question by question */
	char value[160];	/* the deterministic value tokens plus context */
	const char *note;	/* set only when the case could not run */
	int ran;
	int pass;
};

#define NQ 7
static struct outcome q[NQ];
static int debug;

static void trace(const char *what)
{
	if (debug)
		fprintf(stderr, "hijack: %s\n", what);
}

/* ---- timing ------------------------------------------------------------- */

static double cyc_per_ns = 1.0;

static uint64_t rdtsc(void)
{
	return __builtin_ia32_rdtsc();
}

/* Calibrate the TSC against QPC over a short settle, so a cycle delta can be
 * reported in nanoseconds. The frequency rides along as context; it is not a
 * finding and moves with the part. */
static void calibrate(void)
{
	LARGE_INTEGER f, a, b;
	uint64_t t0, t1;
	double qpc_ns;

	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	t0 = rdtsc();
	Sleep(60);
	t1 = rdtsc();
	QueryPerformanceCounter(&b);
	qpc_ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart;
	if (qpc_ns > 0.0)
		cyc_per_ns = (double)(t1 - t0) / qpc_ns;
	if (cyc_per_ns <= 0.0)
		cyc_per_ns = 1.0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y ? 1 : 0;
}

/* ---- the delivery engine ------------------------------------------------ */

static struct hij spin_w __attribute__((aligned(64)));

static DWORD WINAPI spin_proc(LPVOID p)
{
	spin_target((struct hij *)p);
	return 0;
}

static DWORD WINAPI flag_proc(LPVOID p)
{
	flag_target((struct hij *)p);
	return 0;
}

/* Catch spin_target inside its loop, where %rsp equals the value it recorded,
 * so an offset the driver computes is measured from the watched stack pointer.
 * The leaf never moves %rsp once running is set, so this is exact. */
static int catch_spin(HANDLE t, CONTEXT *ctx)
{
	int i;

	for (i = 0; i < 4000000; i++) {
		if (NtSuspend(t, NULL) < 0)
			return -1;
		memset(ctx, 0, sizeof *ctx);
		ctx->ContextFlags = CONTEXT_FULL;
		if (NtGetCtx(t, ctx) >= 0 && spin_w.running &&
		    ctx->Rsp == spin_w.rsp)
			return 0;
		NtResume(t, NULL);
		SwitchToThread();
	}
	return -1;
}

static int wait_done(uint64_t mark)
{
	uint64_t spins = 0;

	while (handler_done == mark) {
		if (++spins > 400000000ull)
			return -1;
		YieldProcessor();
	}
	return 0;
}

/*
 * One delivery into the spinning thread. Catch it, stamp the decision, rewrite
 * %rip to the stub and %rsp to the chosen frame, resume, wait for the handler
 * to return, then suspend and restore the saved context whole -- the sigreturn.
 * reserve is the bytes held below the interrupted %rsp before the frame; naive
 * passes zero and clobbers, the reserved path passes 128 and must not. A
 * latency sample, when wanted, is the cycles from the rewrite to the stub rdtsc.
 */
static int deliver(HANDLE t, uint64_t reserve, uint64_t *sample_cyc)
{
	CONTEXT save, ctx;
	uint64_t before = handler_done, decide;

	if (catch_spin(t, &save))
		return -1;
	ctx = save;
	ctx.Rsp = save.Rsp - reserve;
	ctx.Rip = (DWORD64)(uintptr_t)deliver_stub;
	decide = rdtsc();
	if (NtSetCtx(t, &ctx) < 0) {
		NtResume(t, NULL);
		return -1;
	}
	NtResume(t, NULL);
	if (wait_done(before))
		return -1;
	if (sample_cyc)
		*sample_cyc = t_handler - decide;

	if (NtSuspend(t, NULL) < 0)
		return -1;
	save.ContextFlags = CONTEXT_FULL;
	if (NtSetCtx(t, &save) < 0) {
		NtResume(t, NULL);
		return -1;
	}
	NtResume(t, NULL);
	return 0;
}

static HANDLE start_spin(void)
{
	HANDLE t;

	memset(&spin_w, 0, sizeof spin_w);
	spin_w.depth = 1024;
	t = CreateThread(NULL, 0, spin_proc, &spin_w, 0, NULL);
	if (!t)
		return NULL;
	while (!spin_w.running)
		SwitchToThread();
	return t;
}

static void stop_spin(HANDLE t)
{
	Sleep(50);
	spin_w.stop = 1;
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
}

/* ---- q1, q2: the spinning user thread and its red zone ------------------ */

/*
 * The reserved deliveries carry q1 and q2 at once: q1 that the handler ran and
 * the thread came back to its loop, q2 that the frame 128 below %rsp left the
 * red zone whole. The naive control proves the watcher is not simply blind --
 * a frame at the interrupted %rsp must lose the word at offset 8, the way the
 * real sigdelayed does, or a clean reserved run would mean nothing.
 */
static void case_spin(unsigned long events)
{
	uint64_t c_before, d_before, i_before;
	uint64_t naive_shallow, res_shallow;
	unsigned long i, delivered = 0, naive_ok = 0;
	HANDLE t;

	t = start_spin();
	if (!t) {
		q[0].note = q[1].note = "could not start the spinning thread";
		return;
	}
	for (i = 0; i < events / 4 + 1; i++)
		if (deliver(t, 0, NULL) == 0)
			naive_ok++;
	Sleep(60);			/* let the leaf scan the last clobber before we read */
	naive_shallow = spin_w.shallowest;
	stop_spin(t);

	t = start_spin();
	if (!t) {
		q[0].note = q[1].note = "could not restart the spinning thread";
		return;
	}
	c_before = handler_calls;
	d_before = handler_done;
	for (i = 0; i < events; i++)
		if (deliver(t, 128, NULL) == 0)
			delivered++;
	i_before = spin_w.iters;
	Sleep(60);			/* let it run on, and scan the last delivery */
	res_shallow = spin_w.shallowest;
	q[0].ran = 1;
	q[0].pass = (delivered == events) &&
		    (handler_calls - c_before == delivered) &&
		    (handler_done - d_before == delivered) &&
		    spin_w.running && spin_w.iters > i_before;
	snprintf(q[0].value, sizeof q[0].value,
		 "%s,handler-ran,clean-return,deliveries:%lu,iters-after:%llu",
		 q[0].pass ? "hijacked" : "MISSED",
		 delivered, (unsigned long long)(spin_w.iters - i_before));

	q[1].ran = 1;
	q[1].pass = (res_shallow > 128) && (naive_shallow == 8);
	snprintf(q[1].value, sizeof q[1].value,
		 "%s,reserved-nearest:%llu,naive-nearest:%llu",
		 q[1].pass ? "redzone-intact" : "redzone-BREACHED",
		 (unsigned long long)res_shallow,
		 (unsigned long long)naive_shallow);
	stop_spin(t);
	(void)naive_ok;
	trace("spin done");
}

/* ---- q3: a thread parked in a kernel wait ------------------------------- */

static HANDLE g_ev;
static volatile long wait_woke;

static DWORD WINAPI wait_proc(LPVOID p)
{
	LARGE_INTEGER to;
	struct hij *w = p;

	w->running = 1;
	to.QuadPart = -600000000ll;	/* 60 s, relative */
	NtWait(g_ev, FALSE, &to);
	InterlockedExchange(&wait_woke, 1);
	return 0;
}

/* Is an address inside ntdll? A thread blocked in NtWaitForSingleObject reports
 * a user %rip at the syscall's return inside the ntdll stub, if the model
 * holds; the driver reads the loaded image's own size to check. */
static int in_ntdll(uint64_t rip)
{
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	uint64_t base = (uint64_t)(uintptr_t)nt;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)nt;
	IMAGE_NT_HEADERS *pe;

	if (!nt)
		return 0;
	pe = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	return rip >= base && rip < base + pe->OptionalHeader.SizeOfImage;
}

static void case_wait(void)
{
	static struct hij ww __attribute__((aligned(64)));
	CONTEXT save, ctx;
	HANDLE t;
	uint64_t before, rip;
	int usermode, ntdll, immediate = 0, post = 0;
	long spins;

	g_ev = CreateEventW(NULL, TRUE, FALSE, NULL);	/* manual reset */
	if (!g_ev) {
		q[2].note = "could not create the wait event";
		return;
	}
	memset(&ww, 0, sizeof ww);
	t = CreateThread(NULL, 0, wait_proc, &ww, 0, NULL);
	if (!t) {
		q[2].note = "could not start the waiting thread";
		return;
	}
	while (!ww.running)
		SwitchToThread();
	Sleep(80);				/* let it park in the wait */

	if (NtSuspend(t, NULL) < 0) {
		q[2].note = "could not suspend the waiting thread";
		return;
	}
	memset(&save, 0, sizeof save);
	save.ContextFlags = CONTEXT_FULL;
	if (NtGetCtx(t, &save) < 0) {
		NtResume(t, NULL);
		q[2].note = "could not read the waiting thread's context";
		return;
	}
	rip = save.Rip;
	usermode = rip < 0x00007fffffff0000ull;
	ntdll = in_ntdll(rip);

	before = handler_calls;
	ctx = save;
	ctx.Rsp = save.Rsp - 256;
	ctx.Rip = (DWORD64)(uintptr_t)deliver_stub;
	if (NtSetCtx(t, &ctx) < 0) {
		NtResume(t, NULL);
		q[2].note = "could not rewrite the waiting thread's context";
		return;
	}
	NtResume(t, NULL);

	/* does the rewrite take hold immediately, before the wait is released? */
	for (spins = 0; spins < 400; spins++) {
		if (handler_calls != before) {
			immediate = 1;
			break;
		}
		Sleep(1);
	}

	if (!immediate) {
		SetEvent(g_ev);			/* release the wait */
		for (spins = 0; spins < 2000; spins++) {
			if (handler_calls != before) {
				post = 1;
				break;
			}
			Sleep(1);
		}
	}

	/* clean up: lift the stub out and let the thread unwind and exit */
	if (NtSuspend(t, NULL) >= 0) {
		save.ContextFlags = CONTEXT_FULL;
		NtSetCtx(t, &save);
		NtResume(t, NULL);
	}
	SetEvent(g_ev);
	WaitForSingleObject(t, 3000);
	CloseHandle(t);
	CloseHandle(g_ev);

	q[2].ran = 1;
	/* the design wants the in-kernel case deferred to the gate's exit path;
	 * a rewrite that fires only once the wait returns is exactly that. */
	q[2].pass = usermode && !immediate && post;
	snprintf(q[2].value, sizeof q[2].value,
		 "%s,%s,rewrite-%s,rip:0x%llx",
		 usermode ? "user-rip-at-syscall-return" : "non-user-rip",
		 ntdll ? "in-ntdll" : "outside-ntdll",
		 immediate ? "took-effect-immediately" :
			     post ? "deferred-to-wait-return" : "never-delivered",
		 (unsigned long long)rip);
}

/* ---- q4: the in-kernel flag --------------------------------------------- */

/*
 * Suspend the flag thread again and again, and each time ask whether the flag
 * it left up or down agrees with where its %rip actually stopped. A delivery
 * decided on a lowered flag that caught the thread inside the guarded region is
 * the race the design fears; count them over enough passes that one would show.
 * The check observes rather than rewrites, to keep the model target intact --
 * the dangerous case is flag-down while %rip is inside the region, which the
 * observation captures whole.
 */
static void case_flag(unsigned long iters)
{
	static struct hij fw __attribute__((aligned(64)));
	CONTEXT ctx;
	HANDLE t;
	uint64_t delivered = 0, deferred = 0, violations = 0;
	unsigned long i;

	memset(&fw, 0, sizeof fw);
	t = CreateThread(NULL, 0, flag_proc, &fw, 0, NULL);
	if (!t) {
		q[3].note = "could not start the flag thread";
		return;
	}
	while (!fw.running || fw.flagiters < 4)
		SwitchToThread();

	for (i = 0; i < iters; i++) {
		int in_crit, flag;

		if (NtSuspend(t, NULL) < 0)
			continue;
		memset(&ctx, 0, sizeof ctx);
		ctx.ContextFlags = CONTEXT_CONTROL;
		if (NtGetCtx(t, &ctx) < 0) {
			NtResume(t, NULL);
			continue;
		}
		flag = (int)fw.in_kernel;
		in_crit = ctx.Rip >= fw.crit_lo && ctx.Rip < fw.crit_hi;
		NtResume(t, NULL);
		if (flag) {
			deferred++;
		} else {
			delivered++;
			if (in_crit)
				violations++;
		}
		if ((i & 63) == 0)
			SwitchToThread();
	}
	fw.stop = 1;
	WaitForSingleObject(t, 10000);
	CloseHandle(t);

	q[3].ran = 1;
	q[3].pass = (violations == 0) && (delivered > 0) && (deferred > 0);
	snprintf(q[3].value, sizeof q[3].value,
		 "%s,violations:%llu,delivered:%llu,deferred:%llu",
		 violations == 0 ? "flag-reliable" : "flag-LEAKY",
		 (unsigned long long)violations,
		 (unsigned long long)delivered,
		 (unsigned long long)deferred);
}

/* ---- q5: user-mode shadow stacks ---------------------------------------- */

/* GetProcessMitigationPolicy's shadow-stack query, its enumerator declared by
 * hand because a given mingw header may not carry it on every install. */
#ifndef ProcessUserShadowStackPolicy
#define ProcessUserShadowStackPolicy 15
#endif

static void case_cet(int rewrite_held)
{
	DWORD flags = 0;
	int enabled, ipval;
	typedef BOOL (WINAPI *gpmp_t)(HANDLE, int, PVOID, SIZE_T);
	gpmp_t gp;

	gp = (gpmp_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
					    "GetProcessMitigationPolicy");
	q[4].ran = 1;
	q[4].pass = rewrite_held;
	/* the policy is one DWORD of bit flags: bit 0 EnableUserShadowStack,
	 * bit 2 SetContextIpValidation, the bit that would police this rewrite */
	if (gp && gp(GetCurrentProcess(), ProcessUserShadowStackPolicy,
		     &flags, sizeof flags)) {
		enabled = flags & 1;
		ipval = (flags >> 2) & 1;
		snprintf(q[4].value, sizeof q[4].value,
			 "%s,ip-validation:%s,rip-rewrite-%s,flags:0x%lx",
			 enabled ? "shadow-stack-on" : "shadow-stack-off",
			 ipval ? "on" : "off",
			 rewrite_held ? "holds" : "FAILED",
			 (unsigned long)flags);
	} else {
		snprintf(q[4].value, sizeof q[4].value,
			 "policy-query-unavailable,rip-rewrite-%s",
			 rewrite_held ? "holds" : "FAILED");
	}
}

/* ---- q6: the APC alternative -------------------------------------------- */

static volatile long apc_spin_ran;
static volatile long apc_ctrl_ran;
static volatile long ctrl_stop;

static VOID NTAPI apc_spin_fn(PVOID a, PVOID b, PVOID c)
{
	(void)a; (void)b; (void)c;
	InterlockedIncrement(&apc_spin_ran);
}

static VOID NTAPI apc_ctrl_fn(PVOID a, PVOID b, PVOID c)
{
	(void)a; (void)b; (void)c;
	InterlockedIncrement(&apc_ctrl_ran);
}

static DWORD WINAPI alertable_proc(LPVOID p)
{
	(void)p;
	while (!ctrl_stop) {
		LARGE_INTEGER to;

		to.QuadPart = -100000ll;		/* 10 ms, alertable */
		NtDelay(TRUE, &to);
	}
	return 0;
}

/*
 * Queue user APCs at the spinning thread, which never enters an alertable wait,
 * and confirm none run -- the mechanism the design rejects, measured rather
 * than recalled. The alertable control proves the queue itself works: the same
 * APC aimed at a thread that does wait alertably drains at once.
 */
static void case_apc(unsigned long shots)
{
	HANDLE t, c;
	unsigned long i;
	long spin_ran, ctrl_ran;

	q[5].ran = 1;
	t = start_spin();
	if (!t) {
		q[5].note = "could not start the spinning thread";
		return;
	}
	apc_spin_ran = 0;
	for (i = 0; i < shots; i++)
		NtApc(t, (PVOID)(void *)apc_spin_fn, NULL, NULL, NULL);
	Sleep(200);
	spin_ran = apc_spin_ran;
	stop_spin(t);

	apc_ctrl_ran = 0;
	ctrl_stop = 0;
	c = CreateThread(NULL, 0, alertable_proc, NULL, 0, NULL);
	if (!c) {
		q[5].note = "could not start the alertable control thread";
		return;
	}
	Sleep(20);
	NtApc(c, (PVOID)(void *)apc_ctrl_fn, NULL, NULL, NULL);
	Sleep(200);
	ctrl_ran = apc_ctrl_ran;
	ctrl_stop = 1;
	WaitForSingleObject(c, 3000);
	CloseHandle(c);

	q[5].pass = (spin_ran == 0) && (ctrl_ran >= 1);
	snprintf(q[5].value, sizeof q[5].value,
		 "%s,queued:%lu,ran-on-spinning:%ld,ran-on-alertable:%ld",
		 (spin_ran == 0 && ctrl_ran >= 1) ? "never-runs-on-spinning-thread" :
			spin_ran ? "ran-without-alertable-wait" : "control-never-ran",
		 shots, spin_ran, ctrl_ran);
}

/* ---- q7: latency -------------------------------------------------------- */

static void case_latency(unsigned long shots)
{
	uint64_t *s;
	HANDLE t;
	unsigned long i, n = 0;
	double med_ns, p99_ns;

	s = malloc(shots * sizeof *s);
	if (!s) {
		q[6].note = "out of memory for latency samples";
		return;
	}
	t = start_spin();
	if (!t) {
		q[6].note = "could not start the spinning thread";
		free(s);
		return;
	}
	for (i = 0; i < shots; i++) {
		uint64_t cyc;

		if (deliver(t, 128, &cyc) == 0)
			s[n++] = cyc;
	}
	stop_spin(t);

	q[6].ran = 1;
	if (n < shots / 2) {
		q[6].note = "too few latency samples completed";
		free(s);
		return;
	}
	qsort(s, n, sizeof *s, cmp_u64);
	med_ns = (double)s[n / 2] / cyc_per_ns;
	p99_ns = (double)s[(n * 99) / 100] / cyc_per_ns;
	free(s);

	/* a measurement, not a finding: the token is the word, the numbers ride
	 * along and are free to move between runs */
	q[6].pass = 1;
	snprintf(q[6].value, sizeof q[6].value,
		 "measured,median_ns:%.0f,p99_ns:%.0f,samples:%lu",
		 med_ns, p99_ns, n);
}

/* ---- the finding and the report ----------------------------------------- */

static const char *finding_word(void)
{
	int user_ok = q[0].pass && q[1].pass;
	int kernel_deferred = q[2].ran && q[2].pass;
	int kernel_immediate = q[2].ran && !q[2].pass &&
			       strstr(q[2].value, "immediately") != NULL;

	if (!user_ok)
		return "hijack-refused";
	if (kernel_immediate)
		return "hijack-delivers";
	if (kernel_deferred)
		return "hijack-delivers-user-defers-kernel";
	return "hijack-delivers-user-only";
}

static const char *state(const struct outcome *o)
{
	if (o->note)
		return "unrun";
	return o->pass ? "pass" : "fail";
}

static void report(FILE *out, int terse)
{
	const char *finding = finding_word();
	int failed = 0, incomplete = 0, i;
	const char *verdict;

	for (i = 0; i < NQ; i++) {
		if (q[i].note)
			incomplete++;
		else if (!q[i].pass && i != 6)	/* q7 is measurement-only */
			failed++;
	}
	verdict = incomplete ? "incomplete" : failed ? "no" : "yes";

	if (!terse) {
		fputs("reading, question by question\n\n", out);
		for (i = 0; i < NQ; i++) {
			if (q[i].note)
				fprintf(out, "  %s -- did not run: %s\n",
					q[i].key, q[i].note);
			else
				fprintf(out, "  %s -- %s [%s]\n",
					q[i].reading, q[i].value, state(&q[i]));
		}
		fputs("\nraw\n\n", out);
	}

	for (i = 0; i < NQ; i++) {
		const char *pre = terse ? "" : "    ";

		if (q[i].note)
			fprintf(out, "%s%s=unrun\n", pre, q[i].key);
		else
			fprintf(out, "%s%s=%s\n", pre, q[i].key, q[i].value);
	}

	if (!terse)
		fputs("\nverdict\n\n", out);
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < NQ; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", q[i].key, state(&q[i]));
	fputc('\n', out);
	fprintf(out, "%sverdict=%s\n", terse ? "" : "    ", verdict);
	fprintf(out, "%sfinding=%s\n", terse ? "" : "    ", finding);
	fprintf(out, "%sprobe=%s\n", terse ? "" : "    ", PROBE_VERSION);
}

/* ---- driving ------------------------------------------------------------ */

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  hijack [options]\n"
	      "\n"
	      "Options:\n"
	      "  -e N, --events=N    Deliveries into the spinning thread. [default: 2000]\n"
	      "  -f N, --flag=N      Flag-thread suspensions. [default: 20000]\n"
	      "  -l N, --latency=N   Timed deliveries. [default: 2000]\n"
	      "  -a N, --apc=N       APCs queued at the spinning thread. [default: 2000]\n"
	      "  -t, --terse         The raw block and verdict alone, one key=value per line.\n"
	      "  -d, --debug         Name each case on stderr as it runs.\n"
	      "  -V, --version       Print the version and exit.\n"
	      "  -h, --help          Print this message and exit.\n", out);
}

static long numeric(const char *what, const char *sv)
{
	char *end;
	long v;

	v = strtol(sv, &end, 0);
	if (!*sv || *end || v <= 0) {
		fprintf(stderr, "hijack: %s wants a positive number, not %s\n",
			what, sv);
		exit(2);
	}
	return v;
}

int main(int argc, char **argv)
{
	unsigned long events = 2000, flagn = 20000, latn = 2000, apcn = 2000;
	int terse = 0, i;

	q[0].key = "q1_spinning_user_thread";
	q[0].reading = "q1 a thread spinning in user mode is hijacked, the handler runs, and it returns to its loop";
	q[1].key = "q2_frame_below_redzone";
	q[1].reading = "q2 the frame sits below the reserved 128 and the interrupted red zone survives the return";
	q[2].key = "q3_thread_in_syscall";
	q[2].reading = "q3 a thread blocked in a kernel wait reports where, and its rewrite takes hold when";
	q[3].key = "q4_in_kernel_flag";
	q[3].reading = "q4 the in-kernel flag seen after suspend describes where the thread stopped";
	q[4].key = "q5_cet_shadow_stack";
	q[4].reading = "q5 the %rip rewrite against this build's shadow-stack configuration";
	q[5].key = "q6_apc_alternative";
	q[5].reading = "q6 a user APC at the spinning thread, the alternative the design rejects";
	q[6].key = "q7_latency_ns";
	q[6].reading = "q7 latency from the decision to deliver to the handler's first instruction";

	for (i = 1; i < argc; i++) {
		char *a = argv[i], *val = NULL;

#define OPT(s_, l_) (!strcmp(a, s_) || !strcmp(a, l_) || \
		     (!strncmp(a, l_ "=", strlen(l_) + 1) && (val = a + strlen(l_) + 1)))
#define ARG(w_) (val ? val : (++i < argc ? argv[i] : \
		(fprintf(stderr, "hijack: %s wants a value\n", w_), exit(2), (char *)NULL)))
		if (OPT("-h", "--help")) { usage(stdout); return 0; }
		else if (OPT("-V", "--version")) { puts(PROBE_VERSION); return 0; }
		else if (OPT("-t", "--terse")) terse = 1;
		else if (OPT("-d", "--debug")) debug = 1;
		else if (OPT("-e", "--events")) events = numeric("--events", ARG("--events"));
		else if (OPT("-f", "--flag")) flagn = numeric("--flag", ARG("--flag"));
		else if (OPT("-l", "--latency")) latn = numeric("--latency", ARG("--latency"));
		else if (OPT("-a", "--apc")) apcn = numeric("--apc", ARG("--apc"));
		else {
			fprintf(stderr, "hijack: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
#undef OPT
#undef ARG
	}

	resolve_ntdll();
	if (!ntdll_ok()) {
		fprintf(stderr, "hijack: could not resolve the ntdll thread calls\n");
		return 3;
	}
	calibrate();

	case_spin(events);
	case_wait();
	case_flag(flagn);
	case_cet(q[0].pass);		/* the rewrite held if q1 delivered */
	case_apc(apcn);
	case_latency(latn);

	report(stdout, terse);
	for (i = 0; i < NQ; i++)
		if (q[i].note)
			return 1;
	return q[0].pass ? 0 : 1;
}

/*
 * handler_c -- the C the stub calls once it has the frame, so "the handler ran"
 * means real compiled code executed and returned, not merely that %rip moved.
 * It is ms_abi on this target, which is what the stub's shadow-space call
 * expects, and it writes a modest span of its own frame -- well below the
 * reserved red zone -- so a delivery does realistic work rather than nothing.
 */
__attribute__((noinline))
void handler_c(void)
{
	volatile unsigned char buf[256];
	unsigned i;

	for (i = 0; i < sizeof buf; i++)
		buf[i] = (unsigned char)(i ^ 0x3c);
	if (buf[buf[7] & 255] == 0xffu)
		buf[0] = 0;		/* unreachable; anchors buf */
}
