/*
 * map-cygtls.c -- map the used and unused words of the real _cygtls
 * reservation, so WP-30's carrier word can be placed in genuine pad.
 *
 * The re-measurement (remeasure-my-tls.c) established that below StackBase the
 * address space is either the live _cygtls block [StackBase-PADSIZE, StackBase)
 * or live/ancestor stack below it. A runtime-owned carrier word therefore has
 * to live inside the _cygtls reservation, in a slot Cygwin does not itself use.
 * CYGTLS_PADSIZE (0x3200) is deliberately larger than sizeof(_cygtls); the
 * surplus is headroom this probe locates rather than guesses.
 *
 * For the main thread, a fresh thread, a synchronously delivered signal and a
 * signal delivered on an alternate stack, it scans the reservation one 8-byte
 * word at a time and prints, for each word offset below StackBase, whether the
 * word is ever nonzero and whether it ever changes between the four readings.
 * A word that is zero at every reading and never changes is Cygwin pad and safe
 * to carry the thread pointer; the probe reports the contiguous safe band
 * nearest StackBase, which is where the carrier goes.
 *
 * Build: gcc -O2 -std=gnu11 -Wall -Wextra -o map-cygtls map-cygtls.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/cygwin.h>

#define TEB_STACKBASE 0x08

static inline uint64_t gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off));
	return v;
}

static uint64_t g_pad;
#define MAXW 4096			/* PADSIZE/8 fits well under this */

/* snapshot[k] = word at StackBase - 8*(k+1), i.e. k=0 is the word just below
 * StackBase, increasing k walks down toward cygtls_base. */
static void snapshot(uint64_t *out, unsigned nwords)
{
	uint64_t base = gs_read(TEB_STACKBASE);
	unsigned k;
	for (k = 0; k < nwords; k++)
		out[k] = *(volatile uint64_t *)(base - 8 * (k + 1));
}

static uint64_t snap_start[MAXW];
static uint64_t snap_sigsync[MAXW];
static uint64_t snap_sigalt[MAXW];
static unsigned g_nwords;

static volatile sig_atomic_t which_sig;
static void on_signal(int signo)
{
	(void)signo;
	if (which_sig == 1)
		snapshot(snap_sigsync, g_nwords);
	else
		snapshot(snap_sigalt, g_nwords);
}

/* Report the contiguous band, starting at the word just below StackBase, whose
 * words are zero at thread start and unchanged by either signal. That band is
 * Cygwin pad; its depth in bytes below StackBase is what the carrier can use. */
static void analyse(const char *who)
{
	unsigned k, safe = 0;
	int broke = 0;
	for (k = 0; k < g_nwords; k++) {
		int zero = snap_start[k] == 0;
		int stable = snap_start[k] == snap_sigsync[k] &&
			     snap_start[k] == snap_sigalt[k];
		if (zero && stable && !broke)
			safe = k + 1;
		else
			broke = 1;
	}
	printf("%s.padsize=%llu\n", who, (unsigned long long)g_pad);
	printf("%s.safe_words_below_base=%u\n", who, safe);
	printf("%s.safe_bytes_below_base=%u\n", who, safe * 8);
	/* first nonzero-or-changing word: the top of Cygwin's used region */
	printf("%s.first_used_word_off=0x%x\n", who, safe ? (safe) * 8 : 0);
}

static void run_here(const char *who)
{
	stack_t ss;
	struct sigaction sa;
	static char altbuf[SIGSTKSZ > 65536 ? SIGSTKSZ : 65536];

	snapshot(snap_start, g_nwords);

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGUSR1, &sa, NULL);
	which_sig = 1;
	raise(SIGUSR1);

	memset(&ss, 0, sizeof ss);
	ss.ss_sp = altbuf; ss.ss_size = sizeof altbuf; ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGUSR1, &sa, NULL);
	which_sig = 2;
	raise(SIGUSR1);

	analyse(who);
}

static void *thread_body(void *arg)
{
	(void)arg;
	run_here("thread");
	return NULL;
}

int main(void)
{
	pthread_t th;
	g_pad = (uint64_t)cygwin_internal(CW_CYGTLS_PADSIZE);
	g_nwords = (unsigned)(g_pad / 8);
	if (g_nwords > MAXW) g_nwords = MAXW;

	run_here("main");
	pthread_create(&th, NULL, thread_body, NULL);
	pthread_join(th, NULL);
	return 0;
}
