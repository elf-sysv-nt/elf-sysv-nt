/*
 * fault-probe -- what happens to a fault taken beneath a System V frame.
 *
 * `spike/abi-crossing`'s fault-through case passed on Cygwin 3.0.7 and fails
 * on 3.6.10, and three work packages fail with it. The issue reports read that
 * as 3.6.10 delivering the fault differently. This asks the question in
 * pieces, because the case as written cannot tell three different failures
 * apart: a fault that was never raised, a fault that was raised and lost, and
 * a fault that came back.
 *
 * One case per process, on purpose. A case that loses the fault takes the
 * process with it, so a probe that ran them all in one would print one row and
 * the rest of the matrix would be the row that killed it.
 *
 * Exit status is the answer: 0 recovered, 1 no fault was ever raised, 2 a
 * usage error, 3 an observation case that reports rather than faults. Anything
 * else means the process did not get to answer.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PROBE_VERSION "fault-probe 1.0"

enum { GOT_RECOVERED = 0, GOT_NO_FAULT = 1, GOT_USAGE = 2, GOT_OBSERVED = 3 };

static sigjmp_buf fault_return;
static volatile sig_atomic_t fault_signo;

static void catch_fault(int signo)
{
	fault_signo = signo;
	siglongjmp(fault_return, 1);
}

static void install(int signo, void (*h)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = h;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	sigaction(signo, &sa, NULL);
}

/*
 * Two ways to write the same fault, and the difference between them is the
 * whole of the reported regression.
 *
 * The first is what the spike and runtime/core/t use: a store through a
 * literal null pointer. It is undefined behaviour, and a compiler that can see
 * the path is entitled to conclude the path is unreachable and delete it. The
 * second takes its address out of a volatile global the compiler cannot fold,
 * so the store stands whatever the optimizer believes.
 */
static volatile uintptr_t bad_address;	/* zero, and not provably so */

#define STORE_NULL	do { *(volatile int *)0 = 1; } while (0)
#define STORE_OPAQUE	do { *(volatile int *)bad_address = 1; } while (0)

__attribute__((ms_abi, noinline))   static void ms_null(void) { STORE_NULL; }
__attribute__((ms_abi, noinline))   static void ms_opaque(void) { STORE_OPAQUE; }
__attribute__((noinline))           static void plain_opaque(void) { STORE_OPAQUE; }

/*
 * The specimen shape, matching spike 3's sysv_faulter exactly: a System V
 * function that takes an argument, calls a Microsoft leaf, and returns the
 * argument. Taking and returning a value is what makes it a non-leaf with a
 * real frame rather than something the compiler can turn into a jump.
 */
__attribute__((sysv_abi, noinline))
static uint64_t sysv_over_ms_null(uint64_t t) { ms_null(); return t; }

__attribute__((sysv_abi, noinline))
static uint64_t sysv_over_ms_opaque(uint64_t t) { ms_opaque(); return t; }

/* The fault taken in the System V frame itself, which is a leaf, and which the
 * host's leaf rule therefore describes correctly by accident. */
__attribute__((sysv_abi, noinline))
static uint64_t sysv_direct(uint64_t t) { STORE_OPAQUE; return t; }

typedef __attribute__((sysv_abi)) uint64_t (*sysv_fn)(uint64_t);
static volatile sysv_fn sysv_via_pointer = sysv_over_ms_opaque;

/* Plain frames between the setjmp and the System V frame, direct and through
 * a pointer. Nothing here changes what faults or where; only how it is
 * reached, which is exactly what the matrix is for. */
__attribute__((noinline)) static void wrap1(void) { (void)sysv_over_ms_opaque(1); }
__attribute__((noinline)) static void wrap2(void) { wrap1(); }
__attribute__((noinline)) static void iwrap1(void) { (void)sysv_via_pointer(1); }
__attribute__((noinline)) static void iwrap2(void) { iwrap1(); }

static volatile void (*plain_via_pointer)(void);

/* ---- the observation case --------------------------------------------- */

/*
 * Does the host have an unwind record for these frames? DR-0012 measured this
 * on gcc 7.4 and it is restated here because the seam's second half stops
 * holding the day a System V frame starts carrying one, and this probe runs
 * under a compiler DR-0012 never saw.
 */
__attribute__((noinline))
static uint64_t sink(uint64_t t) { return t ^ bad_address; }

/* Non-leaf on purpose: a leaf needs no record, and the host's leaf rule
 * describes it correctly anyway, so a leaf would answer an easier question
 * than the one asked. */
__attribute__((ms_abi, noinline))
static uint64_t ms_nonleaf(uint64_t t) { return sink(t) + sink(t + 1); }

__attribute__((sysv_abi, noinline))
static uint64_t sysv_nonleaf(uint64_t t) { return sink(t) + sink(t + 1); }

static int has_record(void *fn)
{
	ULONG64 base = 0;

	return RtlLookupFunctionEntry((ULONG64)(uintptr_t)fn, &base, NULL) != NULL;
}

static int observe(void)
{
	printf("ms_frame_carries_unwind_record=%s\n",
	       has_record((void *)ms_nonleaf) ? "yes" : "no");
	printf("sysv_frame_carries_unwind_record=%s\n",
	       has_record((void *)sysv_nonleaf) ? "yes" : "no");
	printf("plain_frame_carries_unwind_record=%s\n",
	       has_record((void *)plain_opaque) ? "yes" : "no");
	return GOT_OBSERVED;
}

/* ---- the cases --------------------------------------------------------- */

static void run_case(const char *name)
{
	if (!strcmp(name, "plain"))            plain_opaque();
	else if (!strcmp(name, "sysv-leaf"))   (void)sysv_direct(1);
	else if (!strcmp(name, "ms-only"))     ms_opaque();
	else if (!strcmp(name, "null-store"))  (void)sysv_over_ms_null(1);
	else if (!strcmp(name, "direct-0"))    (void)sysv_over_ms_opaque(1);
	else if (!strcmp(name, "direct-1"))    wrap1();
	else if (!strcmp(name, "direct-2"))    wrap2();
	else if (!strcmp(name, "pointer-0"))   (void)sysv_via_pointer(1);
	else if (!strcmp(name, "pointer-1"))   { plain_via_pointer = wrap1; plain_via_pointer(); }
	else if (!strcmp(name, "pointer-2"))   { plain_via_pointer = iwrap2; plain_via_pointer(); }
}

static const char *cases[] = {
	"plain", "ms-only", "sysv-leaf", "null-store",
	"direct-0", "direct-1", "direct-2",
	"pointer-0", "pointer-1", "pointer-2",
	NULL
};

static void usage(FILE *out)
{
	int i;

	fputs("Usage:\n"
	      "  fault-probe [options] <case>\n"
	      "\n"
	      "Options:\n"
	      "  -l, --list     List the case names and exit.\n"
	      "  -V, --version  Print the version and exit.\n"
	      "  -h, --help     Print this message and exit.\n"
	      "\n"
	      "Exit status: 0 the fault came back as a signal, 1 no fault was\n"
	      "raised at all, 2 a usage error, 3 an observation was printed.\n"
	      "Any other status means the process did not survive to answer.\n"
	      "\n"
	      "Cases:\n  observe\n", out);
	for (i = 0; cases[i]; i++)
		fprintf(out, "  %s\n", cases[i]);
}

int main(int argc, char **argv)
{
	const char *name;
	volatile int arrived = 0;
	int i;

	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage(stdout);
		return 0;
	}
	if (argc == 2 && (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version"))) {
		puts(PROBE_VERSION);
		return 0;
	}
	if (argc == 2 && (!strcmp(argv[1], "-l") || !strcmp(argv[1], "--list"))) {
		puts("observe");
		for (i = 0; cases[i]; i++)
			puts(cases[i]);
		return 0;
	}
	if (argc != 2) {
		usage(stderr);
		return GOT_USAGE;
	}
	name = argv[1];
	bad_address = 0;

	if (!strcmp(name, "observe"))
		return observe();

	/* The name is checked here rather than inside the case, because a
	 * local set between the sigsetjmp and the siglongjmp comes back
	 * indeterminate unless it is volatile, and a probe that answers
	 * "no such case" for a case it just ran is worse than one that
	 * crashes. */
	for (i = 0; cases[i]; i++)
		if (!strcmp(cases[i], name))
			break;
	if (!cases[i]) {
		fprintf(stderr, "fault-probe: no case named %s\n", name);
		return GOT_USAGE;
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	install(SIGSEGV, catch_fault);
	install(SIGILL, catch_fault);
	install(SIGBUS, catch_fault);
	if (sigsetjmp(fault_return, 1) == 0)
		run_case(name);
	else
		arrived = 1;
	install(SIGSEGV, SIG_DFL);
	install(SIGILL, SIG_DFL);
	install(SIGBUS, SIG_DFL);

	if (arrived) {
		printf("signal=%d\n", (int)fault_signo);
		return GOT_RECOVERED;
	}
	printf("signal=none\n");
	return GOT_NO_FAULT;
}
