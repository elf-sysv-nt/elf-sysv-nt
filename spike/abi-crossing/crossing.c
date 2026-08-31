/*
 * crossing -- can one entry point present a System V face over an MS-ABI core?
 *
 * The design puts the convention change at the bottom of one DLL: everything
 * above it is System V, everything below it is Microsoft x64, and the seam
 * runs through the runtime's own descent into Windows. That is asserted from
 * the Wine precedent and has never been attempted here. This measures it at
 * one function's width, in both directions, and then asks the question the
 * psABI leaves lying in the road: the hundred and twenty-eight bytes below
 * %rsp that Linux promises a signal handler will not touch and Windows has
 * never promised anything about.
 *
 * Two groups of cases. The crossing cases answer yes or no. The red-zone
 * cases answer with a number, because the useful output there is not whethe
 * Windows scribbles below %rsp but where it starts and how far down it goes.
 *
 * Built and driven by abi-crossing.sh. See README.md for the method.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROBE_VERSION "crossing 1.0"

/*
 * On a Microsoft-ABI target, plain va_list is the Microsoft one and va_start
 * in a sysv_abi function is a category error rather than a portability
 * wrinkle. gcc gives the other pair its own names, and the veneer will have
 * to use them for every variadic entry point it exports.
 */
typedef __builtin_sysv_va_list sysv_va_list;
#define sysv_va_start(ap, last)	__builtin_sysv_va_start(ap, last)
#define sysv_va_arg(ap, type)	__builtin_va_arg(ap, type)
#define sysv_va_end(ap)		__builtin_sysv_va_end(ap)

#define SYSV	__attribute__((sysv_abi, noinline))
#define MSABI	__attribute__((ms_abi, noinline))

#define MAX_CASES 16

struct outcome {
	const char *name;
	const char *kind;	/* crossing, control, or redzone */
	const char *note;	/* set when the case could not run at all */
	char detail[192];
	unsigned long long checks;
	unsigned long long failures;
	int ran;
	/* the red-zone cases only: all offsets in bytes below the watched %rsp */
	uint64_t rz_depth;	/* how far down the watcher was looking */
	uint64_t rz_first;	/* the first word it lost */
	uint64_t rz_shallow;	/* the closest word to %rsp it ever lost */
	uint64_t rz_deep;	/* and the furthest */
	uint64_t rz_words;	/* how many words it lost in all */
	uint64_t rz_events;	/* provocations delivered */
};

static struct outcome cases[MAX_CASES];
static int ncases;
static int debug;

static void trace(const char *what)
{
	if (debug)
		fprintf(stderr, "crossing: %s\n", what);
}

static struct outcome *case_open(const char *name, const char *kind)
{
	struct outcome *c = &cases[ncases++];

	c->name = name;
	c->kind = kind;
	c->ran = 1;
	trace(name);
	return c;
}

static void detail(struct outcome *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(c->detail, sizeof c->detail, fmt, ap);
	va_end(ap);
}

/* One check, and the message that is kept when it is the first to fail. */
static void want(struct outcome *c, int ok, const char *fmt, ...)
{
	va_list ap;

	c->checks++;
	if (ok)
		return;
	if (!c->failures) {
		va_start(ap, fmt);
		vsnprintf(c->detail, sizeof c->detail, fmt, ap);
		va_end(ap);
	}
	c->failures++;
}

/* ---- the argument blocks the hand-written callers read ---------------- */

struct sysv_args {
	uint64_t i[6];
	double d[8];
	uint64_t s[2];
};

struct ms_args {
	uint64_t a;
	double b;
	uint64_t c;
	double d;
	uint64_t e;
	double f;
};

/*
 * The red-zone watcher's block. Every offset here is written out again in
 * crossing.S, which cannot include this file, so the two copies are held to
 * each other below rather than by anybody remembering.
 */
struct rz {
	volatile uint64_t stop;
	uint64_t rounds;	/* zero means until stop */
	uint64_t depth;		/* bytes below %rsp to paint, multiple of 8 */
	uint64_t checks;
	uint64_t failures;
	uint64_t first_bad;	/* offset below %rsp of the first word lost */
	uint64_t shallowest;
	uint64_t deepest;
	uint64_t got;
	uint64_t rsp;
	volatile uint64_t running;
	uint64_t mode;		/* 1 faults once a round */
	uint64_t faults;
};

_Static_assert(offsetof(struct rz, stop) == 0, "rz.stop");
_Static_assert(offsetof(struct rz, rounds) == 8, "rz.rounds");
_Static_assert(offsetof(struct rz, depth) == 16, "rz.depth");
_Static_assert(offsetof(struct rz, checks) == 24, "rz.checks");
_Static_assert(offsetof(struct rz, failures) == 32, "rz.failures");
_Static_assert(offsetof(struct rz, first_bad) == 40, "rz.first_bad");
_Static_assert(offsetof(struct rz, shallowest) == 48, "rz.shallowest");
_Static_assert(offsetof(struct rz, deepest) == 56, "rz.deepest");
_Static_assert(offsetof(struct rz, got) == 64, "rz.got");
_Static_assert(offsetof(struct rz, rsp) == 72, "rz.rsp");
_Static_assert(offsetof(struct rz, running) == 80, "rz.running");
_Static_assert(offsetof(struct rz, mode) == 88, "rz.mode");
_Static_assert(offsetof(struct rz, faults) == 96, "rz.faults");
_Static_assert(offsetof(struct sysv_args, d) == 48, "sysv_args.d");
_Static_assert(offsetof(struct sysv_args, s) == 112, "sysv_args.s");
_Static_assert(offsetof(struct ms_args, e) == 32, "ms_args.e");

extern uint64_t sysv_caller_probe(void *fn, const struct sysv_args *a,
				  uint64_t *retval);
extern void ms_caller_probe(void *fn, const struct ms_args *a,
			    uint64_t *retval, uint32_t *gpr, uint32_t *xmm);
extern void redzone_watch(struct rz *w);
extern char redzone_fault_at[];
extern char redzone_fault_resume[];
extern char veh_fault_at[];
extern char veh_fault_resume[];

/*
 * The negative control, selected only by t/run-tests.sh. Everything the
 * register checks claim, they claim by reporting zero, and a check that has
 * quietly stopped looking reports zero too. So the same binary builds a
 * second time with the callee swapped for leaky_face in crossing.S, which
 * returns having destroyed every register it is not entitled to destroy, and
 * the masks have to light up. Watched failing before it is believed passing.
 *
 * The leak has to be assembly. Inline asm inside the C face would be undone
 * on the way out, because gcc restores in its epilogue whatever it saved in
 * its prologue, and the control would pass for the wrong reason.
 */
extern uint64_t leaky_face(void);

#ifdef SPIKE_CLOBBER
#define SYSV_CALLEE ((void *)leaky_face)
#define MS_CALLEE   ((void *)leaky_face)
#else
#define SYSV_CALLEE ((void *)sysv_face)
#define MS_CALLEE   ((void *)ms_face)
#endif

/* ---- the System V face, over a Microsoft core ------------------------- */

static struct sysv_args sysv_sent;
static struct sysv_args sysv_seen;
static unsigned sysv_downcalls;
static char sysv_scratch[64];

#define SYSV_SALT 0x0f1e2d3c4b5a6978ull

/*
 * The entry point under test. It is entered System V -- six integers in
 * registers, eight doubles in registers, two more integers on the stack --
 * and everything it then does is Microsoft x64: a kernel32 call, an ntdll
 * call by way of VirtualQuery, a libc call into a runtime compiled the othe
 * way, and a scheduler call. That is the shape the DLL would have.
 */
SYSV static uint64_t sysv_face(uint64_t a1, uint64_t a2, uint64_t a3,
			       uint64_t a4, uint64_t a5, uint64_t a6,
			       double d1, double d2, double d3, double d4,
			       double d5, double d6, double d7, double d8,
			       uint64_t s1, uint64_t s2)
{
	MEMORY_BASIC_INFORMATION mbi;
	LARGE_INTEGER qpc;
	volatile uint64_t v;

	sysv_seen.i[0] = a1; sysv_seen.i[1] = a2; sysv_seen.i[2] = a3;
	sysv_seen.i[3] = a4; sysv_seen.i[4] = a5; sysv_seen.i[5] = a6;
	sysv_seen.d[0] = d1; sysv_seen.d[1] = d2; sysv_seen.d[2] = d3;
	sysv_seen.d[3] = d4; sysv_seen.d[4] = d5; sysv_seen.d[5] = d6;
	sysv_seen.d[6] = d7; sysv_seen.d[7] = d8;
	sysv_seen.s[0] = s1; sysv_seen.s[1] = s2;

	sysv_downcalls = 0;
	if (GetCurrentThreadId())
		sysv_downcalls++;
	if (VirtualQuery(&mbi, &mbi, sizeof mbi) == sizeof mbi)
		sysv_downcalls++;
	if (QueryPerformanceCounter(&qpc) && qpc.QuadPart)
		sysv_downcalls++;
	if (snprintf(sysv_scratch, sizeof sysv_scratch, "%llu",
		     (unsigned long long)a1) > 0)
		sysv_downcalls++;
	Sleep(0);
	sysv_downcalls++;

	v = a1 + a2 + a3 + a4 + a5 + a6 + s1 + s2 + SYSV_SALT;
	v += (uint64_t)(d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8);
	return v;
}

/* ---- the Microsoft face, over a System V core ------------------------- */

static struct ms_args ms_sent;
static struct ms_args ms_seen;

#define MS_SALT 0x00c0ffee0badf00dull

SYSV static uint64_t sysv_core(uint64_t token, double weight)
{
	Sleep(0);			/* a Microsoft down-call from System V */
	return token ^ (uint64_t)weight ^ MS_SALT;
}

/*
 * Entered Microsoft x64, and what it does is call System V code. This is the
 * direction that leaks: %rsi, %rdi and %xmm6 through %xmm15 are callee-saved
 * to the caller and volatile to the callee, so if the compiler's thunk
 * forgets any of them, the convention escapes out of the bottom of the DLL
 * into a Windows caller that will never know.
 */
MSABI static uint64_t ms_face(uint64_t a, double b, uint64_t c, double d,
			      uint64_t e, double f)
{
	volatile uint64_t v;

	ms_seen.a = a; ms_seen.b = b; ms_seen.c = c;
	ms_seen.d = d; ms_seen.e = e; ms_seen.f = f;

	v = sysv_core(a + c + e, b + d + f);
	return v;
}

/* ---- case: the System V face ------------------------------------------ */

static uint32_t sysv_gpr_mask;

static void case_sysv_face(void)
{
	struct outcome *c = case_open("sysv-face", "crossing");
	uint64_t got = 0, expect;
	uint64_t mask;
	double fsum = 0;
	int i;

	for (i = 0; i < 6; i++)
		sysv_sent.i[i] = 0x1100000000000000ull + (uint64_t)i + 1;
	for (i = 0; i < 8; i++)
		sysv_sent.d[i] = 1.5 * (i + 1);
	sysv_sent.s[0] = 0x2200000000000007ull;
	sysv_sent.s[1] = 0x2200000000000008ull;
	memset(&sysv_seen, 0, sizeof sysv_seen);

	mask = sysv_caller_probe(SYSV_CALLEE, &sysv_sent, &got);
	sysv_gpr_mask = (uint32_t)mask;

	for (i = 0; i < 6; i++)
		want(c, sysv_seen.i[i] == sysv_sent.i[i],
		     "integer argument %d arrived as 0x%llx, not 0x%llx", i + 1,
		     (unsigned long long)sysv_seen.i[i],
		     (unsigned long long)sysv_sent.i[i]);
	for (i = 0; i < 8; i++) {
		want(c, sysv_seen.d[i] == sysv_sent.d[i],
		     "double argument %d arrived as %g, not %g", i + 1,
		     sysv_seen.d[i], sysv_sent.d[i]);
		fsum += sysv_sent.d[i];
	}
	for (i = 0; i < 2; i++)
		want(c, sysv_seen.s[i] == sysv_sent.s[i],
		     "stack argument %d arrived as 0x%llx, not 0x%llx", i + 7,
		     (unsigned long long)sysv_seen.s[i],
		     (unsigned long long)sysv_sent.s[i]);

	expect = SYSV_SALT + sysv_sent.s[0] + sysv_sent.s[1] + (uint64_t)fsum;
	for (i = 0; i < 6; i++)
		expect += sysv_sent.i[i];
	want(c, got == expect, "returned 0x%llx, not 0x%llx",
	     (unsigned long long)got, (unsigned long long)expect);
	want(c, mask == 0, "callee-saved registers changed, mask 0x%llx",
	     (unsigned long long)mask);
	want(c, sysv_downcalls == 5, "%u of 5 down-calls into Windows landed",
	     sysv_downcalls);
	if (!c->failures)
		detail(c, "16 arguments, 5 down-calls, callee-saved mask 0x%llx",
		       (unsigned long long)mask);
}

/* ---- case: the Microsoft face ----------------------------------------- */

static uint32_t ms_gpr_mask, ms_xmm_mask;

static void case_ms_face(void)
{
	struct outcome *c = case_open("ms-face", "crossing");
	uint64_t got = 0, expect;
	uint32_t gpr = 0, xmm = 0;

	ms_sent.a = 0x3300000000000001ull;
	ms_sent.b = 2.25;
	ms_sent.c = 0x3300000000000003ull;
	ms_sent.d = 4.5;
	ms_sent.e = 0x3300000000000005ull;
	ms_sent.f = 8.75;
	memset(&ms_seen, 0, sizeof ms_seen);

	ms_caller_probe(MS_CALLEE, &ms_sent, &got, &gpr, &xmm);
	ms_gpr_mask = gpr;
	ms_xmm_mask = xmm;

	want(c, ms_seen.a == ms_sent.a && ms_seen.c == ms_sent.c &&
		ms_seen.e == ms_sent.e, "an integer argument did not arrive");
	want(c, ms_seen.b == ms_sent.b && ms_seen.d == ms_sent.d &&
		ms_seen.f == ms_sent.f, "a double argument did not arrive");
	expect = (ms_sent.a + ms_sent.c + ms_sent.e) ^
		 (uint64_t)(ms_sent.b + ms_sent.d + ms_sent.f) ^ MS_SALT;
	want(c, got == expect, "returned 0x%llx, not 0x%llx",
	     (unsigned long long)got, (unsigned long long)expect);
	want(c, gpr == 0, "Microsoft callee-saved GPRs changed, mask 0x%x", gpr);
	want(c, xmm == 0, "xmm6 through xmm15 changed, mask 0x%x", xmm);
	if (!c->failures)
		detail(c, "gpr mask 0x%x, xmm mask 0x%x, rsi rdi and xmm6-15 held",
		       gpr, xmm);
}

/* ---- case: variadic entry points -------------------------------------- */

static sigjmp_buf fault_return;
static volatile sig_atomic_t fault_signo;

static void catch_fault(int signo)
{
	fault_signo = signo;
	siglongjmp(fault_return, 1);
}

static void install(int signo, void (*handler)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	sigaction(signo, &sa, NULL);
}

/*
 * The shape every variadic entry point in the veneer will have. The arguments
 * arrive in System V's register save area and the runtime underneath reads
 * Microsoft's, so the list has to be walked and the values passed on one at a
 * time. There is no forwarding.
 */
SYSV static int sysv_vprint(char *out, size_t n, const char *fmt, ...)
{
	sysv_va_list ap;
	long i1, i2;
	double f1, f2;
	const char *s;

	sysv_va_start(ap, fmt);
	i1 = sysv_va_arg(ap, long);
	f1 = sysv_va_arg(ap, double);
	i2 = sysv_va_arg(ap, long);
	f2 = sysv_va_arg(ap, double);
	s = sysv_va_arg(ap, const char *);
	sysv_va_end(ap);
	return snprintf(out, n, fmt, i1, f1, i2, f2, s);
}

/*
 * What the same list yields to a reader shaped the other way. A Microsoft
 * va_list is a pointer walked eight bytes at a time; a System V one is a
 * descriptor holding two offsets and two pointers. Hand the second to code
 * expecting the first and the first argument it fetches is the pair of
 * offsets read as an integer.
 *
 * The read is done here rather than by calling the runtime's own vsnprintf,
 * which was the first shape of this and which took the process down without
 * a signal anything could catch. The point does not need the runtime: what
 * is being shown is that the first eight bytes of a System V argument list
 * are not the first argument, and that is visible from inside.
 */
SYSV static long sysv_first_as_ms(const char *fmt, ...)
{
	sysv_va_list ap;
	long v;

	sysv_va_start(ap, fmt);
	v = *(long *)(void *)ap;
	sysv_va_end(ap);
	return v;
}

SYSV static long sysv_first_as_sysv(const char *fmt, ...)
{
	sysv_va_list ap;
	long v;

	sysv_va_start(ap, fmt);
	v = sysv_va_arg(ap, long);
	sysv_va_end(ap);
	return v;
}

#define VFMT "%ld %g %ld %g %s"
#define VI1 111L
#define VI2 222L
#define VF1 3.5
#define VF2 4.25
#define VS "tail"

static void case_varargs(void)
{
	struct outcome *c = case_open("varargs", "crossing");
	char want_s[96], got_s[96];
	int rc;

	snprintf(want_s, sizeof want_s, VFMT, VI1, VF1, VI2, VF2, VS);
	memset(got_s, 0, sizeof got_s);
	rc = sysv_vprint(got_s, sizeof got_s, VFMT, VI1, VF1, VI2, VF2, VS);
	want(c, rc == (int)strlen(want_s), "returned %d, not %d", rc,
	     (int)strlen(want_s));
	want(c, strcmp(got_s, want_s) == 0, "printed \"%s\", not \"%s\"",
	     got_s, want_s);
	if (!c->failures)
		detail(c, "unpacked and repacked five arguments into \"%s\"",
		       got_s);
}

static void case_varargs_raw(void)
{
	struct outcome *c = case_open("varargs-raw", "control");
	long right, wrong;

	right = sysv_first_as_sysv(VFMT, VI1, VF1, VI2, VF2, VS);
	wrong = sysv_first_as_ms(VFMT, VI1, VF1, VI2, VF2, VS);

	want(c, right == VI1, "the System V reading gave %ld, not %ld", right, VI1);
	want(c, wrong != VI1,
	     "a Microsoft-shaped reader fetched the right first argument out of "
	     "a System V list, which cannot be right");
	want(c, sizeof(sysv_va_list) != sizeof(va_list),
	     "the two va_list types are the same size, %u bytes",
	     (unsigned)sizeof(va_list));
	if (!c->failures)
		detail(c, "sysv va_list %u bytes, ms %u; first argument reads %ld, not %ld",
		       (unsigned)sizeof(sysv_va_list), (unsigned)sizeof(va_list),
		       wrong, right);
}

/* ---- case: Windows calling in ----------------------------------------- */

/*
 * The treacherous set. Everything above is the runtime being called; this is
 * the runtime being entered, by a caller that knows nothing about System V
 * and cannot be told. Four entrances, each Microsoft x64 by the host's rule
 * and each reaching System V code one frame in: a thread start, a queued APC,
 * a vectored exception handler, and a Cygwin signal handler, which is the one
 * that matters because Cygwin's signal delivery is where the fault machinery
 * and the convention meet.
 */
#define CB_SALT 0x5a5a5a5a5aull

static volatile LONG cb_calls, cb_ok;

SYSV static uint64_t sysv_reached(uint64_t token)
{
	Sleep(0);
	return token ^ CB_SALT;
}

static void cb_do(uint64_t token)
{
	InterlockedIncrement(&cb_calls);
	if (sysv_reached(token) == (token ^ CB_SALT))
		InterlockedIncrement(&cb_ok);
}

static DWORD WINAPI cb_thread(LPVOID arg)
{
	cb_do((uint64_t)(uintptr_t)arg);
	return 0;
}

static VOID CALLBACK cb_apc(ULONG_PTR arg)
{
	cb_do((uint64_t)arg);
}

static void cb_signal(int signo)
{
	(void)signo;
	cb_do(0x51);
}

static volatile LONG veh_hits;

__attribute__((noinline, noclone))
static void veh_trigger(void)
{
	__asm__ __volatile__(
		"xorl %%r11d, %%r11d\n\t"
		".globl veh_fault_at\n\t"
		"veh_fault_at:\n\t"
		"movl $1, (%%r11)\n\t"
		".globl veh_fault_resume\n\t"
		"veh_fault_resume:\n\t"
		: : : "r11", "memory");
}

/*
 * One vectored handler for both users. It answers only to the two faults this
 * spike plants, by address rather than by instruction length, and hands
 * everything else to Cygwin, which is where a real access violation belongs.
 */
static LONG CALLBACK spike_veh(EXCEPTION_POINTERS *ep)
{
	CONTEXT *ctx = ep->ContextRecord;

	if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;
	if ((char *)(uintptr_t)ctx->Rip == veh_fault_at) {
		InterlockedIncrement(&veh_hits);
		cb_do(0x76);
		ctx->Rip = (DWORD64)(uintptr_t)veh_fault_resume;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	if ((char *)(uintptr_t)ctx->Rip == redzone_fault_at) {
		ctx->Rip = (DWORD64)(uintptr_t)redzone_fault_resume;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void case_callbacks(void)
{
	struct outcome *c = case_open("callbacks", "crossing");
	HANDLE t;

	cb_calls = cb_ok = 0;

	t = CreateThread(NULL, 0, cb_thread, (LPVOID)(uintptr_t)0x7401, 0, NULL);
	if (!t) {
		c->note = "could not create the thread";
		return;
	}
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
	want(c, cb_calls == 1 && cb_ok == 1, "a thread start did not reach System V code");

	if (!QueueUserAPC(cb_apc, GetCurrentThread(), 0x7402)) {
		c->note = "QueueUserAPC refused";
		return;
	}
	SleepEx(0, TRUE);
	want(c, cb_calls == 2 && cb_ok == 2, "an APC did not reach System V code");

	veh_hits = 0;
	veh_trigger();
	want(c, veh_hits == 1, "the vectored handler did not run");
	want(c, cb_calls == 3 && cb_ok == 3,
	     "a vectored exception handler did not reach System V code");

	install(SIGUSR1, cb_signal);
	raise(SIGUSR1);
	install(SIGUSR1, SIG_DFL);
	want(c, cb_calls == 4 && cb_ok == 4,
	     "a Cygwin signal handler did not reach System V code");
	if (!c->failures)
		detail(c, "thread start, APC, vectored handler and signal handler, %ld of %ld",
		       (long)cb_ok, (long)cb_calls);
}

/* ---- case: a fault underneath a System V frame ------------------------ */

/*
 * The faulting address lives in a volatile global rather than appearing as a
 * literal. It is still zero; what the indirection buys is that the compile
 * cannot prove it is zero. A store through a literal null pointer is
 * undefined behaviour, and gcc 14 proved the path unreachable and removed
 * the call site that reached it, so from 2026-08-30 to 2026-08-31 this case
 * reported a fault that never came back when in fact no fault had been
 * raised. The characterization is in issue/0001 and the transcripts beside
 * it; this is the repair it called for.
 */
static volatile uintptr_t fault_address;	/* zero, and not provably so */

MSABI static void ms_faulter(void)
{
	*(volatile int *)fault_address = 1;
}

SYSV static uint64_t sysv_faulter(uint64_t token)
{
	ms_faulter();
	return token;
}

/*
 * One plain frame between the sigsetjmp and the System V call, and it is a
 * workaround rather than a flourish: gcc 14.4 ICEs in choose_baseaddr when a
 * function that calls sigsetjmp also calls a sysv_abi function it cannot
 * prove unreachable. The characterization hit the same ICE from the othe
 * direction, and its probe took the same detour.
 */
__attribute__((noinline)) static void call_sysv_faulter(void)
{
	(void)sysv_faulter(1);
}

/*
 * The claim this one tests is the one AGENTS.md says nobody may assume: that
 * the runtime keeps its SEH-based fault handling after being re-faced. A
 * store through a null pointer inside Microsoft code, one frame under a
 * System V frame, has to reach Cygwin as SIGSEGV and the handler has to be
 * able to leave by siglongjmp -- which unwinds past the System V frame
 * without asking it anything. Then the crossing is exercised again, because a
 * process that survives the fault and has lost the convention afterwards has
 * not survived it.
 *
 * The two failure modes get two sentences. A case that returns without
 * faulting means the specimen is not in the binary, which is the compiler's
 * doing; a process that never comes back at all is a delivery failure, which
 * is the host's. The first shape of this reported both as the fault not
 * coming back, and the difference was two days of wrong root cause.
 */
static void case_fault_through(void)
{
	struct outcome *c = case_open("fault-through", "crossing");
	uint64_t got = 0, mask;
	int arrived = 0;

	install(SIGSEGV, catch_fault);
	fault_signo = 0;
	if (sigsetjmp(fault_return, 1) == 0)
		call_sysv_faulter();
	else
		arrived = 1;
	install(SIGSEGV, SIG_DFL);

	want(c, arrived, "the faulter returned without faulting; the specimen "
	     "is not in the binary");
	want(c, fault_signo == SIGSEGV, "arrived as signal %d, not SIGSEGV",
	     (int)fault_signo);

	mask = sysv_caller_probe(SYSV_CALLEE, &sysv_sent, &got);
	want(c, mask == 0, "the crossing lost registers afterwards, mask 0x%llx",
	     (unsigned long long)mask);
	if (!c->failures)
		detail(c, "SIGSEGV through a System V frame, and the crossing held after it");
}

/* ---- the red zone ------------------------------------------------------ */

/*
 * The psABI reserves the 128 bytes below %rsp and says they "shall not be
 * modified by signal or interrupt handlers". Linux honors it; nobody has eve
 * claimed Windows does. What follows measures it rather than reasoning about
 * it, because the answer sets a compilation policy for an entire libc and
 * -mno-red-zone is not free.
 *
 * The watcher is entered through a System V frame, since the question is
 * about a signal arriving in the middle of a System V call and not about
 * Windows in general. Every case paints the same region and differs only in
 * what is done to the thread while it watches.
 */
SYSV static void sysv_watch(struct rz *w)
{
	redzone_watch(w);
}

/*
 * Deeper than the 128 bytes the question is about, and deliberately so. The
 * first shape of this watched 128 and reported that Windows' own exception
 * dispatch touched nothing, which is true and useless: the dispatch record
 * starts further down than that, and a watcher that cannot see where it
 * starts cannot tell a red zone Windows respects from one it happens to miss.
 */
static uint64_t rz_depth = 1024;

static void rz_setup(struct rz *w, uint64_t rounds, uint64_t mode)
{
	memset(w, 0, sizeof *w);
	w->rounds = rounds;
	w->depth = rz_depth;
	w->mode = mode;
}

static void rz_collect(struct outcome *c, const struct rz *w, uint64_t events)
{
	/* A red-zone case has no pass or fail to give, so what it lost lands in
	 * rz_words and c->failures stays what the verdict reads. rz-quiet is
	 * the exception and sets its own below. */
	c->checks = w->checks;
	c->failures = 0;
	c->rz_words = w->failures;
	c->rz_depth = w->depth;
	c->rz_first = w->first_bad;
	c->rz_shallow = w->shallowest;
	c->rz_deep = w->deepest;
	c->rz_events = events;
	if (!w->failures)
		detail(c, "%llu bytes below %%rsp intact over %llu passes",
		       (unsigned long long)w->depth,
		       (unsigned long long)w->checks);
	else
		detail(c, "lost bytes %llu through %llu below %%rsp, first at %llu",
		       (unsigned long long)w->shallowest,
		       (unsigned long long)w->deepest,
		       (unsigned long long)w->first_bad);
}

static void case_rz_quiet(unsigned long rounds)
{
	struct outcome *c = case_open("rz-quiet", "control");
	struct rz w;

	rz_setup(&w, rounds, 0);
	sysv_watch(&w);
	rz_collect(c, &w, 0);
	/* The control every measurement below it leans on. A watcher that
	 * cannot see a clobber reports the same zero as a red zone nobody
	 * touched, so this one is judged rather than merely recorded. */
	if (w.failures)
		want(c, 0, "the region moved with nothing provoking it, "
		     "%llu words, the first at offset %llu",
		     (unsigned long long)w.failures,
		     (unsigned long long)w.first_bad);
}

static volatile int burner_stop;

static void *burner_body(void *arg)
{
	volatile unsigned long long *sink = arg;

	while (!burner_stop)
		(*sink)++;
	return NULL;
}

/*
 * Preemption on its own, with a burner on every processor and no signal, no
 * exception and no system call anywhere near the watcher. Windows saves a
 * preempted thread's registers on its kernel stack, so nothing should appea
 * here; the case exists because that is a belief until it is a measurement,
 * and because spike 1 found the analogous belief about %fs to be false.
 */
static void case_rz_preempt(unsigned long rounds, int ncpu)
{
	struct outcome *c = case_open("rz-preempt", "redzone");
	pthread_t *burner = calloc((size_t)ncpu, sizeof *burner);
	unsigned long long sink = 0;
	struct rz w;
	int started = 0, i;

	if (!burner) {
		c->note = "out of memory";
		return;
	}
	burner_stop = 0;
	for (i = 0; i < ncpu; i++)
		if (pthread_create(&burner[i], NULL, burner_body, &sink) == 0)
			started++;
	rz_setup(&w, rounds, 0);
	sysv_watch(&w);
	burner_stop = 1;
	for (i = 0; i < started; i++)
		pthread_join(burner[i], NULL);
	free(burner);
	/* Preemptions cannot be counted from inside the thread they happen to,
	 * so this case puts the burner threads in the events column instead;
	 * the table below says so. */
	rz_collect(c, &w, (uint64_t)started);
}

static struct rz shared_w;

static DWORD WINAPI rz_thread(LPVOID arg)
{
	(void)arg;
	sysv_watch(&shared_w);
	return 0;
}

static void *rz_pthread(void *arg)
{
	(void)arg;
	sysv_watch(&shared_w);
	return NULL;
}

/*
 * Suspend the thread, read its context back, write the same context returned,
 * resume it. This is the mechanism Cygwin uses to deliver a signal, minus the
 * delivery, so it separates the hijack from what the hijack is for.
 */
static void case_rz_hijack(unsigned long rounds)
{
	struct outcome *c = case_open("rz-hijack", "redzone");
	CONTEXT ctx __attribute__((aligned(16)));
	HANDLE t;
	unsigned long i, done = 0;

	rz_setup(&shared_w, 0, 0);
	t = CreateThread(NULL, 0, rz_thread, NULL, 0, NULL);
	if (!t) {
		c->note = "could not create the thread";
		return;
	}
	while (!shared_w.running)
		Sleep(0);
	for (i = 0; i < rounds; i++) {
		if (SuspendThread(t) == (DWORD)-1) {
			c->note = "SuspendThread refused";
			break;
		}
		memset(&ctx, 0, sizeof ctx);
		ctx.ContextFlags = CONTEXT_FULL;
		if (GetThreadContext(t, &ctx) && SetThreadContext(t, &ctx))
			done++;
		ResumeThread(t);
		SwitchToThread();
	}
	shared_w.stop = 1;
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
	rz_collect(c, &shared_w, done);
}

static volatile LONG rz_signals;

static void rz_sig_handler(int signo)
{
	(void)signo;
	InterlockedIncrement(&rz_signals);
}

/*
 * The case the question was really about: a signal delivered to a thread
 * sitting inside a System V call, by the machinery the runtime already uses.
 * The handler does nothing at all, so anything that shows up below %rsp is
 * the cost of delivery rather than the cost of the handler.
 */
static void case_rz_signal(unsigned long rounds)
{
	struct outcome *c = case_open("rz-signal", "redzone");
	pthread_t tid;
	unsigned long i;

	rz_signals = 0;
	rz_setup(&shared_w, 0, 0);
	install(SIGUSR2, rz_sig_handler);
	if (pthread_create(&tid, NULL, rz_pthread, NULL) != 0) {
		c->note = "could not create the thread";
		install(SIGUSR2, SIG_DFL);
		return;
	}
	while (!shared_w.running)
		sched_yield();
	for (i = 0; i < rounds; i++) {
		pthread_kill(tid, SIGUSR2);
		SwitchToThread();
	}
	shared_w.stop = 1;
	pthread_join(tid, NULL);
	install(SIGUSR2, SIG_DFL);
	rz_collect(c, &shared_w, (uint64_t)rz_signals);
}

/*
 * A hardware fault instead of a signal, taken by Windows' own exception
 * dispatch and answered by a vectored handler that steps over it. Cygwin is
 * never involved, so this prices the host's dispatch by itself and says
 * whether the cost measured by rz-signal is Cygwin's or Windows'.
 */
static void case_rz_veh(unsigned long rounds)
{
	struct outcome *c = case_open("rz-veh", "redzone");
	struct rz w;

	rz_setup(&w, rounds, 1);
	sysv_watch(&w);
	rz_collect(c, &w, w.faults);
}

/* ---- the report -------------------------------------------------------- */

static int is_redzone(const struct outcome *c)
{
	return strcmp(c->kind, "redzone") == 0;
}

static const char *state(const struct outcome *c)
{
	if (c->note)
		return "unrun";
	if (is_redzone(c))
		return "measured";
	return c->failures ? "fail" : "pass";
}

static void report(FILE *out, int terse)
{
	int failed = 0, incomplete = 0, judged = 0, i;
	uint64_t shallowest = 0, deepest = 0;
	char clobbered[192];
	const char *verdict;

	clobbered[0] = '\0';
	for (i = 0; i < ncases; i++) {
		struct outcome *c = &cases[i];

		if (c->note) {
			incomplete++;
			continue;
		}
		if (c->rz_words) {
			if (!shallowest || c->rz_shallow < shallowest)
				shallowest = c->rz_shallow;
			if (c->rz_deep > deepest)
				deepest = c->rz_deep;
			if (clobbered[0])
				strncat(clobbered, ",",
					sizeof clobbered - strlen(clobbered) - 1);
			strncat(clobbered, c->name,
				sizeof clobbered - strlen(clobbered) - 1);
		}
		if (is_redzone(c))
			continue;
		judged++;
		if (c->failures)
			failed++;
	}
	if (incomplete)
		verdict = "incomplete";
	else
		verdict = failed ? "no" : "yes";

	if (!terse) {
		fprintf(out, "== the crossing\n\n");
		fprintf(out, "    %-14s %-9s %8s %9s  %s\n",
			"", "kind", "checks", "failures", "what it saw");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];

			if (is_redzone(c))
				continue;
			if (c->note) {
				fprintf(out, "    %-14s %-9s %8s %9s  %s\n",
					c->name, c->kind, "-", "-", c->note);
				continue;
			}
			fprintf(out, "    %-14s %-9s %8llu %9llu  %s\n",
				c->name, c->kind, c->checks, c->failures,
				c->detail);
		}
		fprintf(out, "\n    A crossing case has to pass. A control has to behave as a\n"
			     "    control, which for varargs-raw means producing the wrong\n"
			     "    answer and for rz-quiet means seeing nothing move.\n\n");

		fprintf(out, "== the red zone\n\n");
		fprintf(out, "    %-14s %10s %8s %10s %8s %8s\n",
			"", "passes", "events", "words lost", "nearest", "furthest");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];

			if (!is_redzone(c) && strcmp(c->name, "rz-quiet"))
				continue;
			if (c->note) {
				fprintf(out, "    %-14s %10s  %s\n", c->name, "-", c->note);
				continue;
			}
			fprintf(out, "    %-14s %10llu %8llu %10llu %8llu %8llu\n",
				c->name, c->checks,
				(unsigned long long)c->rz_events,
				(unsigned long long)c->rz_words,
				(unsigned long long)c->rz_shallow,
				(unsigned long long)c->rz_deep);
		}
		fprintf(out, "\n    Nearest and furthest are distances below the watched %%rsp,\n"
			     "    in bytes. The watcher only ever looked %llu bytes down, so a\n"
			     "    furthest equal to that is a floor and not a measurement, and\n"
			     "    the furthest under rz-veh counts this probe's own handler\n"
			     "    frames as well as the host's. Nearest is the number that\n"
			     "    decides the policy, because it is where the writing starts.\n"
			     "    Under rz-preempt the events column counts burner threads:\n"
			     "    preemptions cannot be counted from the thread they hit.\n\n",
			(unsigned long long)rz_depth);

		fprintf(out, "== summary\n\n");
	}
#define K(fmt, ...) fprintf(out, "%s" fmt "\n", terse ? "" : "    ", __VA_ARGS__)
	K("verdict=%s", verdict);
	K("cases=%d", ncases);
	K("cases_judged=%d", judged);
	K("cases_failed=%d", failed);
	K("cases_incomplete=%d", incomplete);
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < ncases; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", cases[i].name,
			state(&cases[i]));
	fputc('\n', out);
	K("sysv_callee_saved_mask=0x%x", sysv_gpr_mask);
	K("ms_callee_saved_gpr_mask=0x%x", ms_gpr_mask);
	K("ms_callee_saved_xmm_mask=0x%x", ms_xmm_mask);
	K("redzone_watched_bytes=%llu", (unsigned long long)rz_depth);
	for (i = 0; i < ncases; i++) {
		struct outcome *c = &cases[i];

		if (!is_redzone(c) && strcmp(c->name, "rz-quiet"))
			continue;
		if (c->note)
			K("redzone_%s=unrun", c->name + 3);
		else if (!c->rz_words)
			K("redzone_%s=intact", c->name + 3);
		else
			fprintf(out, "%sredzone_%s=nearest:%llu,furthest:%llu,words:%llu\n",
				terse ? "" : "    ", c->name + 3,
				(unsigned long long)c->rz_shallow,
				(unsigned long long)c->rz_deep,
				(unsigned long long)c->rz_words);
	}
	K("redzone_clobbered_by=%s", clobbered[0] ? clobbered : "nothing");
	K("redzone_nearest_byte=%llu", (unsigned long long)shallowest);
	K("redzone_furthest_byte=%llu", (unsigned long long)deepest);
	/* The line the DLL's build flags turn on. The psABI reserves 128
	 * bytes; anything landing at or above that offset means code compiled
	 * with a red zone would have its scratch overwritten. */
	K("redzone_policy=%s",
	  (shallowest && shallowest <= 128) ? "mno-red-zone required"
					    : "no clobber inside 128 bytes");
	K("probe=%s", PROBE_VERSION);
#undef K
}

/* ---- driving ----------------------------------------------------------- */

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  crossing [options]\n"
	      "\n"
	      "Options:\n"
	      "  -r N, --rounds=N   Passes for the quiet and preempt cases. [default: 200000]\n"
	      "  -e N, --events=N   Provocations for hijack, signal and vectored. [default: 2000]\n"
	      "  -z N, --depth=N    Bytes below %rsp to watch, multiple of 8. [default: 1024]\n"
	      "  -c NAME, --case=NAME  Run one case rather than all eleven.\n"
	      "  -t, --terse        The summary block alone, one key=value per line.\n"
	      "  -d, --debug        Name each case on stderr as it starts.\n"
	      "  -V, --version      Print the version and exit.\n"
	      "  -h, --help         Print this message and exit.\n", out);
}

static const char *only;

static int selected(const char *name)
{
	return !only || strcmp(only, name) == 0;
}

static long numeric(const char *what, const char *s)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 0);
	if (errno || !*s || *end || v <= 0) {
		fprintf(stderr, "crossing: %s wants a positive number, not %s\n",
			what, s);
		exit(2);
	}
	return v;
}

static const char *known[] = {
	"sysv-face", "ms-face", "varargs", "varargs-raw", "callbacks",
	"fault-through", "rz-quiet", "rz-preempt", "rz-hijack", "rz-signal",
	"rz-veh", NULL
};

int main(int argc, char **argv)
{
	unsigned long rounds = 200000, events = 2000;
	int terse = 0, ncpu, i;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		char *val = NULL;

#define OPT(short_, long_)						\
		(!strcmp(a, short_) || !strcmp(a, long_) ||		\
		 (!strncmp(a, long_ "=", strlen(long_) + 1) &&		\
		  (val = a + strlen(long_) + 1)))
#define ARG(what)							\
		(val ? val : (++i < argc ? argv[i] :			\
			     (fprintf(stderr, "crossing: %s wants a value\n", what), \
			      exit(2), (char *)NULL)))

		if (OPT("-h", "--help")) { usage(stdout); return 0; }
		else if (OPT("-V", "--version")) { puts(PROBE_VERSION); return 0; }
		else if (OPT("-t", "--terse")) terse = 1;
		else if (OPT("-d", "--debug")) debug = 1;
		else if (OPT("-r", "--rounds"))
			rounds = (unsigned long)numeric("--rounds", ARG("--rounds"));
		else if (OPT("-e", "--events"))
			events = (unsigned long)numeric("--events", ARG("--events"));
		else if (OPT("-z", "--depth"))
			rz_depth = (uint64_t)numeric("--depth", ARG("--depth"));
		else if (OPT("-c", "--case"))
			only = ARG("--case");
		else {
			fprintf(stderr, "crossing: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
#undef OPT
#undef ARG
	}

	if (rz_depth < 8 || rz_depth % 8) {
		fprintf(stderr, "crossing: --depth wants a positive multiple of 8\n");
		return 2;
	}
	if (only) {
		for (i = 0; known[i]; i++)
			if (!strcmp(known[i], only))
				break;
		if (!known[i]) {
			fprintf(stderr, "crossing: no case named %s\n", only);
			return 2;
		}
	}

	ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 1)
		ncpu = 1;

	if (!AddVectoredExceptionHandler(1, spike_veh)) {
		fprintf(stderr, "crossing: the vectored handler would not install\n");
		return 1;
	}

	if (selected("sysv-face"))	case_sysv_face();
	if (selected("ms-face"))	case_ms_face();
	if (selected("varargs"))	case_varargs();
	if (selected("varargs-raw"))	case_varargs_raw();
	if (selected("callbacks"))	case_callbacks();
	if (selected("fault-through"))	case_fault_through();
	if (selected("rz-quiet"))	case_rz_quiet(rounds);
	if (selected("rz-preempt"))	case_rz_preempt(rounds, ncpu);
	if (selected("rz-hijack"))	case_rz_hijack(events);
	if (selected("rz-signal"))	case_rz_signal(events);
	if (selected("rz-veh"))		case_rz_veh(events);

	report(stdout, terse);
	for (i = 0; i < ncases; i++)
		if (cases[i].note || (strcmp(cases[i].kind, "redzone") &&
				      cases[i].failures))
			return 1;
	return 0;
}
