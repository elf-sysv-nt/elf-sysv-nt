/*
 * core_test -- WP-22's certification, in the shape spike 3 left.
 *
 * It exercises the host-facing entry points of runtime/core: that Windows
 * calling in through each of them reaches System V code one frame down and
 * returns; that the callee-saved set each convention promises survives the
 * crossing in both directions; that the host recognizes the SEH unwind data on
 * every ms_abi entry point and, by design, does not on a sysv_abi core; and
 * that a fault taken beneath a System V frame reaches Cygwin's signal machinery
 * and returns. The register probes are hand-written in probe.S for the reason
 * spike 3 gives; the two leaky targets there are the controls that make the
 * register checks prove they can fail.
 *
 * Built and driven by t/run.sh, -mno-red-zone, with the host gcc that targets
 * x86_64-pc-cygwin. See README.md.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../core.h"

#define PROBE_VERSION "core-cross 1.0"

/* The hand-written register probes and their leaky controls, from probe.S. */
extern uint32_t ms_cross_probe(void *fn, uint64_t arg);
extern uint32_t sysv_cross_probe(void *fn, uint64_t arg);
extern void elfsysv_leaky_ms(uint64_t);
extern void elfsysv_leaky_sysv(uint64_t);

/* The full masks the two probes report when every callee-saved register is
 * clobbered: eighteen bits for the Microsoft set, six for the System V set. */
#define MS_FULL_MASK   0x3ffffu
#define SYSV_FULL_MASK 0x3fu

#define MAX_CASES 16

struct outcome {
	const char *name;
	const char *kind;	/* crossing, control */
	const char *note;
	char detail[192];
	unsigned checks;
	unsigned failures;
	int ran;
};

static struct outcome cases[MAX_CASES];
static int ncases;
static uint32_t last_ms_mask, last_sysv_mask;

static struct outcome *case_open(const char *name, const char *kind)
{
	struct outcome *c = &cases[ncases++];
	c->name = name;
	c->kind = kind;
	c->ran = 1;
	return c;
}

static void detail(struct outcome *c, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(c->detail, sizeof c->detail, fmt, ap);
	va_end(ap);
}

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

/* ---- the crossings, register-checked ---------------------------------- */

/*
 * Windows calling an ms_abi entry point. ms_cross_probe stands in for the
 * Windows caller: it poisons the full Microsoft callee-saved set, calls the
 * entry with a token, and reports what changed. The entry reaches the System V
 * core and returns; nothing in the Microsoft set may have moved. Run over the
 * three single-argument entry points, which is every entry whose one argument
 * the probe can supply in %rcx.
 */
static void case_ms_crossing(void)
{
	struct outcome *c = case_open("ms-crossing", "crossing");
	struct { const char *n; void *fn; } e[] = {
		{ "thread_entry", (void *)elfsysv_thread_entry },
		{ "apc",          (void *)elfsysv_apc },
		{ "signal_entry", (void *)elfsysv_signal_entry },
	};
	uint32_t acc = 0;
	unsigned i;

	for (i = 0; i < sizeof e / sizeof e[0]; i++) {
		uint64_t before = elfsysv_core_calls;
		uint32_t m = ms_cross_probe(e[i].fn, 0x1111000000000000ull + i);
		acc |= m;
		want(c, m == 0, "%s changed the Microsoft callee-saved set, mask 0x%x",
		     e[i].n, m);
		want(c, elfsysv_core_calls == before + 1,
		     "%s did not reach the System V core", e[i].n);
	}
	last_ms_mask = acc;
	if (!c->failures)
		detail(c, "rbx rbp rsi rdi r12-r15 xmm6-15 intact across three entry points");
}

static void case_ms_crossing_control(void)
{
	struct outcome *c = case_open("ms-crossing-control", "control");
	uint32_t m = ms_cross_probe((void *)elfsysv_leaky_ms, 0);

	want(c, m == MS_FULL_MASK,
	     "the leaky Microsoft target lit mask 0x%x, not 0x%x", m, MS_FULL_MASK);
	if (!c->failures)
		detail(c, "leaky target lit all eighteen bits, so a partial leak would show");
}

/*
 * The runtime reaching a System V body. sysv_cross_probe poisons the System V
 * callee-saved set, calls the core, and reports what changed; the core does a
 * Microsoft down-call (Sleep) and returns, and nothing in the System V set may
 * have moved.
 */
static void case_sysv_crossing(void)
{
	struct outcome *c = case_open("sysv-crossing", "crossing");
	uint64_t before = elfsysv_core_calls;
	uint32_t m = sysv_cross_probe((void *)elfsysv_core_run, 0x2222000000000000ull);

	last_sysv_mask = m;
	want(c, m == 0, "the System V callee-saved set changed, mask 0x%x", m);
	want(c, elfsysv_core_calls == before + 1, "the core did not run");
	if (!c->failures)
		detail(c, "rbx rbp r12-r15 intact across a System V down-call and back");
}

static void case_sysv_crossing_control(void)
{
	struct outcome *c = case_open("sysv-crossing-control", "control");
	uint32_t m = sysv_cross_probe((void *)elfsysv_leaky_sysv, 0);

	want(c, m == SYSV_FULL_MASK,
	     "the leaky System V target lit mask 0x%x, not 0x%x", m, SYSV_FULL_MASK);
	if (!c->failures)
		detail(c, "leaky target lit all six bits");
}

/* ---- the unwind seam --------------------------------------------------- */

/*
 * Every host-facing entry point must carry unwind data the host recognizes,
 * because Cygwin's exception and signal delivery is the host's own SEH walking
 * MS-format records. This asks the host directly: elfsysv_core_unwind_present
 * calls RtlLookupFunctionEntry, the function the dispatcher itself calls, and a
 * RUNTIME_FUNCTION coming back is the host saying it can walk this frame.
 */
static void case_unwind_present(void)
{
	struct outcome *c = case_open("unwind-present", "crossing");
	struct { const char *n; void *fn; } e[] = {
		{ "dllmain",      (void *)elfsysv_dllmain },
		{ "thread_entry", (void *)elfsysv_thread_entry },
		{ "apc",          (void *)elfsysv_apc },
		{ "tls_callback", (void *)elfsysv_tls_callback },
		{ "veh",          (void *)elfsysv_veh },
		{ "signal_entry", (void *)elfsysv_signal_entry },
	};
	unsigned i, have = 0;

	for (i = 0; i < sizeof e / sizeof e[0]; i++) {
		int ok = elfsysv_core_unwind_present(e[i].fn);
		have += ok ? 1 : 0;
		want(c, ok, "%s carries no unwind record the host recognizes", e[i].n);
	}
	if (!c->failures)
		detail(c, "all %u ms_abi entry points have a host RUNTIME_FUNCTION", have);
}

/*
 * And the other half of the seam: a sysv_abi core carries no record the host
 * recognizes, so the host treats a System V frame as a leaf and cannot walk it.
 * This is the invariant WP-23 and WP-43 rest on -- no host unwinder is ever
 * pointed through a System V frame -- so it is asserted rather than left
 * implicit. A System V frame that started carrying host unwind data would be a
 * silent change to what the host will try to walk, and this fails if it does.
 */
static void case_unwind_seam(void)
{
	struct outcome *c = case_open("unwind-seam", "crossing");
	int present = elfsysv_core_unwind_present((void *)elfsysv_core_run);

	want(c, !present,
	     "a System V core carries host unwind data, which breaks the seam");
	if (!c->failures)
		detail(c, "the System V core is a leaf to the host unwinder, as the seam requires");
}

/* ---- Windows calling in, four ways ------------------------------------- */

#define CB_ARG 0x00c0ffee0000ULL

__attribute__((noinline, noclone))
static void plant_fault(void)
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

extern char veh_fault_at[];
extern char veh_fault_resume[];

/* The resolver runs after elfsysv_veh has continued the search: it steps the
 * planted fault by address and resumes, so the process survives the case. */
static LONG CALLBACK resolver_veh(EXCEPTION_POINTERS *ep)
{
	CONTEXT *ctx = ep->ContextRecord;
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
	    (char *)(uintptr_t)ctx->Rip == veh_fault_at) {
		ctx->Rip = (DWORD64)(uintptr_t)veh_fault_resume;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void case_callbacks(void)
{
	struct outcome *c = case_open("callbacks", "crossing");
	HANDLE t, veh1, veh2;
	DWORD code = 0;
	uint64_t before;

	/* A thread start. The exit code is the core's return value carried back
	 * out through the ms_abi boundary, so a correct code is a full round
	 * trip observed. */
	before = elfsysv_core_calls;
	t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)elfsysv_thread_entry,
			 (LPVOID)(uintptr_t)CB_ARG, 0, NULL);
	if (!t) { c->note = "could not create the thread"; return; }
	WaitForSingleObject(t, 10000);
	GetExitCodeThread(t, &code);
	CloseHandle(t);
	want(c, elfsysv_core_calls == before + 1, "the thread start did not reach the core");
	want(c, code == (DWORD)((CB_ARG ^ 0xE1F5C0DE00000000ull) & 0xffffffffu),
	     "the thread returned 0x%lx, not the core's value", (unsigned long)code);

	/* A queued APC. */
	before = elfsysv_core_calls;
	if (!QueueUserAPC((PAPCFUNC)elfsysv_apc, GetCurrentThread(), (ULONG_PTR)CB_ARG)) {
		c->note = "QueueUserAPC refused"; return;
	}
	SleepEx(0, TRUE);
	want(c, elfsysv_core_calls == before + 1, "the APC did not reach the core");
	want(c, elfsysv_core_last_token == CB_ARG, "the APC reached the core with the wrong token");

	/* A vectored exception handler. elfsysv_veh is installed first, reaches
	 * the core, and continues the search; the resolver steps the fault. */
	before = elfsysv_core_calls;
	veh1 = AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)elfsysv_veh);
	veh2 = AddVectoredExceptionHandler(0, resolver_veh);
	if (!veh1 || !veh2) { c->note = "a vectored handler would not install"; return; }
	plant_fault();
	want(c, elfsysv_core_calls >= before + 1, "the vectored handler did not reach the core");
	RemoveVectoredExceptionHandler(veh2);
	RemoveVectoredExceptionHandler(veh1);

	/* A Cygwin signal handler, which is the entrance that matters because the
	 * fault machinery and the convention meet there. */
	before = elfsysv_core_calls;
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		*(void **)&sa.sa_handler = (void *)elfsysv_signal_entry;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_NODEFER;
		sigaction(SIGUSR1, &sa, NULL);
		raise(SIGUSR1);
		signal(SIGUSR1, SIG_DFL);
	}
	want(c, elfsysv_core_calls == before + 1, "the signal handler did not reach the core");
	want(c, elfsysv_core_last_token == (uint64_t)SIGUSR1,
	     "the signal handler reached the core with token %llu, not %d",
	     (unsigned long long)elfsysv_core_last_token, SIGUSR1);

	if (!c->failures)
		detail(c, "thread start, APC, vectored handler and signal handler each reached System V code");
}

/* ---- a fault beneath a System V frame ---------------------------------- */

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

ELFSYSV_MSABI static void ms_faulter(void)
{
	*(volatile int *)0 = 1;
}

ELFSYSV_SYSV static uint64_t sysv_over_ms(uint64_t t)
{
	ms_faulter();
	return t;
}

ELFSYSV_SYSV static uint64_t sysv_direct_fault(uint64_t t)
{
	*(volatile int *)0 = 1;
	return t;
}

/*
 * The headline. A store through a null pointer inside Microsoft code, one frame
 * beneath a System V frame, has to reach Cygwin as SIGSEGV and the handler has
 * to be able to leave by siglongjmp -- which returns past the System V frame
 * without asking it to unwind, the frame the host cannot walk. Then a crossing
 * is exercised again, because a process that survived the fault and lost the
 * convention afterwards has not survived it.
 */
static void case_fault_through(void)
{
	struct outcome *c = case_open("fault-through", "crossing");
	int arrived = 0;
	uint32_t m;

	install(SIGSEGV, catch_fault);
	fault_signo = 0;
	if (sigsetjmp(fault_return, 1) == 0)
		(void)sysv_over_ms(1);
	else
		arrived = 1;
	install(SIGSEGV, SIG_DFL);

	want(c, arrived, "the fault never came back as a signal");
	want(c, fault_signo == SIGSEGV, "arrived as signal %d, not SIGSEGV", (int)fault_signo);

	m = ms_cross_probe((void *)elfsysv_thread_entry, 0x9);
	want(c, m == 0, "the crossing lost registers after the fault, mask 0x%x", m);
	if (!c->failures)
		detail(c, "SIGSEGV through a System V frame, and the crossing held after it");
}

/*
 * The same, but the fault is taken directly in the System V frame -- the frame
 * that carries no host unwind record at all. Spike 3 faulted in Microsoft code
 * on purpose and left this untested; it matters because it shows the host's
 * delivery does not depend on the faulting instruction sitting in a frame the
 * host can walk. The fault still reaches Cygwin and still returns.
 */
static void case_fault_direct_sysv(void)
{
	struct outcome *c = case_open("fault-direct-sysv", "crossing");
	int arrived = 0;

	want(c, !elfsysv_core_unwind_present((void *)sysv_direct_fault),
	     "the System V faulter unexpectedly carries host unwind data");

	install(SIGSEGV, catch_fault);
	fault_signo = 0;
	if (sigsetjmp(fault_return, 1) == 0)
		(void)sysv_direct_fault(1);
	else
		arrived = 1;
	install(SIGSEGV, SIG_DFL);

	want(c, arrived, "the fault never came back as a signal");
	want(c, fault_signo == SIGSEGV, "arrived as signal %d, not SIGSEGV", (int)fault_signo);
	if (!c->failures)
		detail(c, "a fault in an unwalkable System V frame still reached Cygwin and returned");
}

/* ---- the report -------------------------------------------------------- */

static void report(FILE *out, int terse)
{
	int failed = 0, i;
	const char *verdict;

	for (i = 0; i < ncases; i++)
		if (cases[i].note || cases[i].failures)
			failed++;
	verdict = failed ? "no" : "yes";

	if (!terse) {
		fprintf(out, "== the host-facing core\n\n");
		fprintf(out, "    %-22s %-9s %7s %9s  %s\n",
			"", "kind", "checks", "failures", "what it saw");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];
			if (c->note) {
				fprintf(out, "    %-22s %-9s %7s %9s  %s\n",
					c->name, c->kind, "-", "-", c->note);
				continue;
			}
			fprintf(out, "    %-22s %-9s %7u %9u  %s\n",
				c->name, c->kind, c->checks, c->failures, c->detail);
		}
		fprintf(out, "\n    A crossing case has to pass. A control has to light the mask\n"
			     "    it is built to light, so the register check is known able to fail.\n\n");
		fprintf(out, "== summary\n\n");
	}
#define K(fmt, ...) fprintf(out, "%s" fmt "\n", terse ? "" : "    ", __VA_ARGS__)
	K("verdict=%s", verdict);
	K("cases=%d", ncases);
	K("cases_failed=%d", failed);
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < ncases; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", cases[i].name,
			cases[i].note ? "unrun" : (cases[i].failures ? "fail" : "pass"));
	fputc('\n', out);
	K("ms_callee_saved_mask=0x%x", last_ms_mask);
	K("sysv_callee_saved_mask=0x%x", last_sysv_mask);
	K("probe=%s", PROBE_VERSION);
#undef K
}

int main(int argc, char **argv)
{
	int terse = 0, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--terse"))
			terse = 1;
		else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
			puts(PROBE_VERSION); return 0;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			puts("Usage: core_test [-t|--terse] [-V] [-h]"); return 0;
		} else {
			fprintf(stderr, "core_test: unknown option %s\n", argv[i]);
			return 2;
		}
	}

	case_ms_crossing();
	case_ms_crossing_control();
	case_sysv_crossing();
	case_sysv_crossing_control();
	case_unwind_present();
	case_unwind_seam();
	case_callbacks();
	case_fault_through();
	case_fault_direct_sysv();

	report(stdout, terse);
	for (i = 0; i < ncases; i++)
		if (cases[i].note || cases[i].failures)
			return 1;
	return 0;
}
