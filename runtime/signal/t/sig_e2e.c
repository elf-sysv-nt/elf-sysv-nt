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

	double t0 = now_seconds();
	for (long i = 0; i < events; i++) {
		if (elfsysv_sig_hijack(worker_handle, &state, 10, NULL,
				       &pending) != 0) {
			printf("sig_e2e: a host call in the hijack failed\n");
			break;
		}
		/* The target decides, so wait for it before reusing the
		 * record. A delivery that never decides is a hang, and the
		 * bound turns it into a reported failure. */
		long spins = 0;
		while (pending.disposition < 0 && spins++ < 20000000)
			Sleep(0);
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
	}
	r->seconds = now_seconds() - t0;

	stop_flag = 1;
	pthread_join(th, NULL);
	CloseHandle(worker_handle);
	worker_handle = NULL;

	r->fold_held = (spin_out[0] == expected_fold(spin_out[1]));
	r->handler_sp = handler_last_sp;
	if (handler_runs != r->delivered)
		return -2;
	return 0;
}

int main(int argc, char **argv)
{
	long events = 500;
	int quiet = 0;
	int rc = 0;

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

	if (run_arm(0, events, 0, &reserved) != 0) {
		printf("not ok - the reserving arm did not complete\n");
		return 1;
	}
	if (run_arm(1, events, 0, &naive) != 0) {
		printf("not ok - the control arm did not complete\n");
		return 1;
	}
	if (run_arm(0, events, 1, &onalt) != 0) {
		printf("not ok - the alternate-stack arm did not complete\n");
		return 1;
	}

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
