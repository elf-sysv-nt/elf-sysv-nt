/* WP-43 end to end: real deliveries into a real thread, through the host's
 * own hijack, with the red zone under observation.
 *
 * A worker thread runs sig_redzone_spin, a hand-written leaf whose accumulator
 * lives at -8(%rsp) and nowhere else. The main thread suspends it, reads its
 * register file, builds an ELF signal frame in it and resumes it into a
 * handler -- Cygwin's delivery mechanism with this package's frame in it. The
 * handler returns, the trampoline calls elfsysv_sigreturn, the restore iretqs
 * back into the loop, and the loop keeps folding.
 *
 * At the end the fold is recomputed from the seed and the round count. If the
 * accumulator survived every delivery the two agree; if any delivery wrote
 * into the reserved bytes they do not.
 *
 * The control arm is what makes that evidence rather than a hope. The same run
 * repeats with the reservation switched off, which is the naive construction
 * DR-0006 rejected, and the fold is then required to differ. A probe that
 * cannot see the damage when the damage is switched on is not watching the
 * right bytes -- which is exactly how spike/cygwin-from-source's measurement
 * failed, and why this one carries its own control.
 *
 * Usage:
 *   sig_e2e [options]
 *
 * Options:
 *   -n N, --events=N    deliveries per arm [default: 500]
 *   -q, --quiet         errors only
 *   --arm-control       internal: run only the control arm and report on
 *                       stdout; the parent runs this in a child because a
 *                       no-reserve delivery can kill the process
 *   -h, --help          print this message and exit
 *
 * Exit: 0 every arm held, 1 an arm did not, 2 usage.
 *
 * The default event count is not arbitrary. A delivery lands wherever the
 * target happens to be, and only the ones that land in the leaf can damage its
 * accumulator, so the control arm needs enough of them to be sure of hitting
 * that window. Twenty is not enough: at twenty the control has been seen to
 * report a whole red zone with the repair switched off, which is precisely the
 * false pass this arm exists to catch.
 */
#include <windows.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../sigpriv.h"
#include "../sig_host.h"

#define SEED 0x0123456789abcdefull
#define PRIME 0x100000001b3ull

extern ELF_SYSV void sig_redzone_spin(volatile int *stop, uint64_t *out);

/* Bounds of the leaf, from spin.S. A worker whose RIP is in [begin, end) is
 * back in the loop; anywhere else it is still inside a delivery. */
extern char sig_redzone_spin_end[];

/* elfsysv_sig_enter, _return_tramp, _restore and _resume come from signal.h;
 * their addresses place a stuck worker's RIP against the delivery machinery. */

/* The worker's RIP through a suspend/resume, or 0 on a host-call failure.
 * Reads and changes nothing, so it is safe while the worker is mid-delivery. */
static uintptr_t worker_rip(HANDLE h)
{
	CONTEXT c;
	uintptr_t rip;
	int ok;

	memset(&c, 0, sizeof c);
	c.ContextFlags = CONTEXT_CONTROL;
	if (SuspendThread(h) == (DWORD)-1)
		return 0;
	ok = GetThreadContext(h, &c);
	rip = (uintptr_t)c.Rip;
	ResumeThread(h);
	return ok ? rip : 0;
}

/* 1 if the worker is executing inside the leaf, 0 if not. */
static int worker_in_spin(HANDLE h)
{
	uintptr_t rip = worker_rip(h);
	return (rip >= (uintptr_t)(void *)sig_redzone_spin &&
		rip < (uintptr_t)(void *)sig_redzone_spin_end) ? 1 : 0;
}

static volatile int stop_flag;
static uint64_t spin_out[2];
static HANDLE worker_handle;
static volatile int worker_ready;
static elfsysv_sigstate_t state;

static volatile long handler_runs;
static volatile uintptr_t handler_last_sp;

static ELF_SYSV void handler(int signo)
{
	uintptr_t sp;
	__asm__ volatile("movq %%rsp, %0" : "=r"(sp));
	handler_last_sp = sp;
	(void)signo;
	handler_runs++;
}

static void *worker(void *arg)
{
	(void)arg;
	elfsysv_sig_current = &state;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			     GetCurrentProcess(), &worker_handle, 0, FALSE,
			     DUPLICATE_SAME_ACCESS))
		worker_handle = NULL;
	worker_ready = 1;
	sig_redzone_spin(&stop_flag, spin_out);
	return NULL;
}

static uint64_t expected_fold(uint64_t rounds)
{
	uint64_t v = SEED;
	for (uint64_t i = 0; i < rounds; i++) {
		v *= PRIME;
		v ^= 0x9e37;
	}
	return v;
}

/* Discriminates a clean exit() from a hard TerminateProcess in a run that
 * dies without printing: atexit handlers run for the former and not the
 * latter, and the delivery count says how far the arm got. */
static void exit_probe(void)
{
	fprintf(stderr, "# exit path: atexit reached, handler_runs=%ld\n",
		(long)handler_runs);
}

static double now_seconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

struct arm_result {
	long delivered;
	long refused;
	int fold_held;
	double seconds;
	uintptr_t handler_sp;
	elf_sig_placement_t last_place;
};

static int run_arm(int no_reserve, long events, int alt, struct arm_result *r)
{
	pthread_t th;
	elfsysv_sigaction_t sa;
	static char alt_stack[128 * 1024];
	elfsysv_stack_t ss;

	memset(r, 0, sizeof(*r));
	stop_flag = 0;
	worker_ready = 0;
	worker_handle = NULL;
	handler_runs = 0;
	spin_out[0] = spin_out[1] = 0;

	elf_sig_init(&state);
	state.measure_no_reserve = no_reserve;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)handler;
	sa.sa_flags = ELFSYSV_SA_NODEFER | (alt ? ELFSYSV_SA_ONSTACK : 0);
	elf_sig_action(&state, 10, &sa, NULL);

	if (alt) {
		memset(&ss, 0, sizeof(ss));
		ss.ss_sp = alt_stack;
		ss.ss_size = sizeof(alt_stack);
		if (elf_sig_altstack(&state, 0, &ss, NULL) != 0) {
			printf("sig_e2e: the alternate stack was refused\n");
			return -1;
		}
	}

	if (pthread_create(&th, NULL, worker, NULL) != 0) {
		printf("sig_e2e: could not start the worker\n");
		return -1;
	}
	while (!worker_ready)
		sched_yield();
	if (!worker_handle) {
		stop_flag = 1;
		pthread_join(th, NULL);
		printf("sig_e2e: could not get a handle on the worker\n");
		return -1;
	}

	static elfsysv_sig_pending_t pending;

	r->seconds = 0.0;
	for (long i = 0; i < events; i++) {
		double t0 = now_seconds();
		if (elfsysv_sig_hijack(worker_handle, &state, 10, NULL,
				       &pending) != 0) {
			printf("sig_e2e: a host call in the hijack failed\n");
			break;
		}
		/* disposition is the receiver's DECISION, not the completion
		 * of the delivery: it is set in elfsysv_sig_enter_c before the
		 * handler runs and before sigreturn restores the thread. Wait
		 * for it, then price the delivery here. */
		long spins = 0;
		while (pending.disposition < 0 && spins++ < 20000000)
			Sleep(0);
		r->seconds += now_seconds() - t0;
		if (pending.disposition < 0) {
			printf("sig_e2e: a delivery never reached the "
			       "target\n");
			r->refused++;
			break;
		}
		if (pending.disposition == (int)ELF_SIG_DELIVERED) {
			r->delivered++;
			r->last_place = pending.where;
		} else
			r->refused++;

		/* Do NOT reuse the record or re-hijack until the delivery has
		 * actually finished — the worker back in the leaf, past the
		 * handler and sigreturn. disposition fires while the worker is
		 * still inside elfsysv_sig_resume reading this same record, so
		 * re-hijacking on it alone lets the next SetThreadContext and
		 * memset race the in-flight restore; at 20000 events that
		 * corruption kills the process about two runs in three. Real
		 * delivery is serialized by the signal mask this test drives
		 * with SA_NODEFER, so the test must serialize it here instead.
		 * This gate is outside the timed section above, so it does not
		 * enter the reservation cost. */
		double gate_t0 = now_seconds();
		while (worker_in_spin(worker_handle) != 1) {
			/* Generous by design: a delivery is tens of
			 * microseconds, so tens of seconds is only ever a
			 * worker that is not coming back, never one that is
			 * merely slow under load. A false STUCK would make the
			 * suite flaky on a busy machine; a real one still
			 * reports here with the RIP that places it. */
			if (now_seconds() - gate_t0 > 30.0) {
				uintptr_t rip = worker_rip(worker_handle);
				fprintf(stderr,
					"# STUCK after delivery %ld: worker rip "
					"%p; enter=%p tramp=%p resume=%p "
					"restore=%p spin=[%p,%p) handler=%p\n",
					i, (void *)rip,
					(void *)elfsysv_sig_enter,
					(void *)elfsysv_sig_return_tramp,
					(void *)elfsysv_sig_resume,
					(void *)elfsysv_sig_restore,
					(void *)sig_redzone_spin,
					(void *)sig_redzone_spin_end,
					(void *)handler);
				printf("sig_e2e: the worker did not return to "
				       "the leaf after a delivery\n");
				r->refused++;
				goto loop_done;
			}
			Sleep(0);
		}

		/* Let the target run free before the next interrupt. Without
		 * this the check above suspends and resumes the worker, and
		 * the next hijack's suspend catches it a few instructions on,
		 * so every delivery would land at the same point in the loop —
		 * which defeats the control arm, since a fixed landing can
		 * consistently miss or heal the clobber. A short, varying
		 * free-run spreads the landings back across the loop the way
		 * an unsynchronised delivery does. It is outside the timed
		 * section, so it does not enter the cost. */
		int freerun = (int)(i & 15) + 1;
		while (freerun--)
			SwitchToThread();
	}
loop_done:
	fprintf(stderr, "# arm loop done, delivered=%ld\n", r->delivered);

	stop_flag = 1;
	pthread_join(th, NULL);
	fprintf(stderr, "# worker joined\n");
	CloseHandle(worker_handle);
	worker_handle = NULL;

	r->fold_held = (spin_out[0] == expected_fold(spin_out[1]));
	r->handler_sp = handler_last_sp;
	if (handler_runs != r->delivered)
		return -2;
	fprintf(stderr, "# arm returning\n");
	return 0;
}

/* The control arm in a child. A no-reserve delivery can land while the worker
 * is mid-return and kill the whole process -- that is the damage DR-0006's
 * repair exists to prevent, demonstrated rather than simulated -- so the arm
 * runs behind a process boundary where its death is evidence instead of a
 * lost run. A dead child counts as the control seeing damage; the timing is
 * taken only from a child that finished. */
static int run_control_child(const char *self, long events,
			     struct arm_result *r, int *deaths)
{
	char cmd[512];
	int tries;

	/* Bounded: a corrupted spin can stop honoring the stop flag, leaving
	 * the child's join waiting forever. A hung control is damage on the
	 * same terms as a dead one, so the watchdog turns it into a death. */
	snprintf(cmd, sizeof cmd, "timeout 120 \"%s\" --arm-control -n %ld",
		 self, events);
	for (tries = 0; tries < 10; tries++) {
		FILE *p = popen(cmd, "r");
		char line[256];
		int got = 0;
		if (!p)
			return -1;
		while (fgets(line, sizeof line, p))
			if (sscanf(line,
				   "CONTROL delivered=%ld seconds=%lf fold=%d",
				   &r->delivered, &r->seconds,
				   &r->fold_held) == 3)
				got = 1;
		pclose(p);
		if (got)
			return 0;
		(*deaths)++;
		fprintf(stderr, "# control-arm child died without reporting "
			"(attempt %d); a dead process is damage too\n",
			tries + 1);
	}
	return -1;
}

int main(int argc, char **argv)
{
	long events = 500;
	int quiet = 0;
	int rc = 0;

	/* Unbuffered, so a run that dies mid-arm leaves its partial output as
	 * evidence instead of losing everything still in the stdio buffer. A
	 * certification probe that can exit silently certifies nothing. */
	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc >= 2 && !strcmp(argv[1], "--arm-control")) {
		struct arm_result r;
		if (argc >= 4 && (!strcmp(argv[2], "-n") ||
				  !strcmp(argv[2], "--events")))
			events = strtol(argv[3], NULL, 0);
		if (run_arm(1, events, 0, &r) != 0)
			return 1;
		printf("CONTROL delivered=%ld seconds=%.9f fold=%d\n",
		       r.delivered, r.seconds, r.fold_held);
		return 0;
	}

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-q") || !strcmp(a, "--quiet"))
			quiet = 1;
		else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			printf("usage: sig_e2e [-n events] [-q]\n");
			return 0;
		} else if ((!strcmp(a, "-n") || !strcmp(a, "--events")) &&
			   i + 1 < argc)
			events = strtol(argv[++i], NULL, 0);
		else {
			printf("sig_e2e: unknown option %s\n", a);
			return 2;
		}
	}

	struct arm_result reserved, naive, onalt;
	int arm_rc, control_deaths = 0;

	/* Parent only, past the child branch: a run that reaches atexit chose
	 * to exit; one that leaves no atexit line was killed outright. */
	atexit(exit_probe);

	/* The arm markers go to stderr, which is never buffered, so the last
	 * marker in a transcript names the arm a dead run died in. */
	fprintf(stderr, "# arm: reserving\n");
	if ((arm_rc = run_arm(0, events, 0, &reserved)) != 0) {
		printf("not ok - the reserving arm did not complete (%d)\n",
		       arm_rc);
		return 1;
	}
	fprintf(stderr, "# arm: control (child)\n");
	if (run_control_child(argv[0], events, &naive, &control_deaths) != 0) {
		printf("not ok - the control arm did not complete "
		       "(%d child deaths)\n", control_deaths);
		return 1;
	}
	fprintf(stderr, "# arm: alternate\n");
	if ((arm_rc = run_arm(0, events, 1, &onalt)) != 0) {
		printf("not ok - the alternate-stack arm did not complete (%d)\n",
		       arm_rc);
		return 1;
	}
	fprintf(stderr, "# arms done\n");

	if (reserved.delivered < events / 2) {
		printf("not ok - too few deliveries landed (%ld of %ld)\n",
		       reserved.delivered, events);
		rc = 1;
	}
	if (!reserved.fold_held) {
		printf("not ok - the red zone did not survive a reserving "
		       "delivery\n");
		rc = 1;
	}
	if (naive.fold_held) {
		printf("not ok - the control arm did not damage the red zone, "
		       "so this probe is not watching it\n");
		rc = 1;
	}
	if (!onalt.fold_held) {
		printf("not ok - the red zone did not survive an "
		       "alternate-stack delivery\n");
		rc = 1;
	}

	double per_reserved = reserved.delivered
				      ? reserved.seconds / reserved.delivered
				      : 0.0;
	double per_naive = naive.delivered ? naive.seconds / naive.delivered
					   : 0.0;
	double delta = per_naive > 0.0
			       ? (per_reserved - per_naive) / per_naive * 100.0
			       : 0.0;

	if (!quiet) {
		printf("reserving:  %ld delivered, red zone %s, %.3f us each\n",
		       reserved.delivered, reserved.fold_held ? "whole"
							     : "broken",
		       per_reserved * 1e6);
		printf("control:    %ld delivered, red zone %s, %.3f us each\n",
		       naive.delivered, naive.fold_held ? "whole" : "broken",
		       per_naive * 1e6);
		if (control_deaths)
			printf("control:    also killed its process %d time(s) "
			       "before a run completed\n", control_deaths);
		printf("alternate:  %ld delivered, red zone %s, handler sp %p\n",
		       onalt.delivered, onalt.fold_held ? "whole" : "broken",
		       (void *)onalt.handler_sp);
		printf("reservation cost: %+.2f%% of a delivery\n", delta);
	}

	if (rc == 0 && !quiet)
		printf("ok - deliveries into a running thread return "
		       "correctly and keep the red zone\n");
	return rc;
}
