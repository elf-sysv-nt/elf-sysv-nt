/*
 * remeasure-my-tls.c -- WP-30's re-measurement of the real Cygwin _my_tls.
 *
 * DR-0003 settled the TLS model as carrier C3: a runtime-owned thread pointer
 * kept a fixed distance below the thread's stack base and reached through %gs
 * as gs:[NtTib.StackBase] then that offset. It also recorded the one carried
 * risk: spike/gs-thread-pointer measured a STAND-IN, not Cygwin's real
 * _my_tls, and it named two things WP-2x/WP-30 must re-measure against the real
 * block -- its padding constant, and its behaviour when Cygwin moves a thread
 * onto an alternate signal stack.
 *
 * This probe measures both, against the running Cygwin rather than a stand-in.
 *
 *   1. The padding constant. Cygwin publishes it: cygwin_internal with the
 *      CW_CYGTLS_PADSIZE query returns CYGTLS_PADSIZE, the distance below
 *      NtTib.StackBase at which _cygtls (the real _my_tls) begins. This is the
 *      authoritative real value, not the spike stand-in's blind one-page guess.
 *
 *   2. Where a runtime-owned carrier word can live without colliding with the
 *      real _cygtls. _cygtls occupies [StackBase - PADSIZE, StackBase). Below
 *      it, between the block and where Cygwin sets the first stack frame, is a
 *      gap the running stack never climbs back into: rsp only descends from its
 *      entry value. The probe reports StackBase, PADSIZE, entry rsp, and the
 *      gap, on the main thread, a fresh thread, and a fork child.
 *
 *   3. The alternate signal stack. When Cygwin delivers a signal with a
 *      sigaltstack installed and SA_ONSTACK set, does NtTib.StackBase in the
 *      TEB change to the alternate stack? If it does, gs:[StackBase] inside the
 *      handler points at a different block and the C3 chain reads the wrong
 *      word; the establishment must then also run at handler entry. The probe
 *      reads StackBase and rsp inside a SA_ONSTACK handler and reports whether
 *      the base moved and whether it left the thread's own stack region.
 *
 * The output is a terse key=value block, the form a decision record quotes.
 * Nothing is installed and no privilege is wanted.
 *
 * Build:  gcc -O2 -std=gnu11 -Wall -Wextra -o remeasure-my-tls remeasure-my-tls.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/cygwin.h>

/* NtTib.StackBase sits at TEB offset 0x08, reached through %gs. Confirmed
 * against the running kernel by spike/gs-thread-pointer, reused here. */
#define TEB_STACKBASE  0x08
#define TEB_STACKLIMIT 0x10

static inline uint64_t gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off));
	return v;
}

static inline uint64_t read_rsp(void)
{
	uint64_t v;
	__asm__ __volatile__("movq %%rsp, %0" : "=r"(v));
	return v;
}

/* CYGTLS_PADSIZE straight from the running Cygwin: the real distance below
 * StackBase at which _cygtls begins. cygwin_internal returns uintptr_t. */
static uint64_t cygtls_padsize(void)
{
	return (uint64_t)cygwin_internal(CW_CYGTLS_PADSIZE);
}

/* One thread's geometry, read wherever it is called. */
struct geom {
	uint64_t base;		/* NtTib.StackBase                          */
	uint64_t limit;		/* NtTib.StackLimit                         */
	uint64_t rsp;		/* rsp at the point of measurement          */
	uint64_t cygtls;	/* StackBase - PADSIZE: where _cygtls begins */
	uint64_t gap;		/* cygtls - rsp: the owned band below _cygtls */
};

static void measure(struct geom *g, uint64_t padsize)
{
	g->base = gs_read(TEB_STACKBASE);
	g->limit = gs_read(TEB_STACKLIMIT);
	g->rsp = read_rsp();
	g->cygtls = g->base - padsize;
	g->gap = (g->cygtls > g->rsp) ? (g->cygtls - g->rsp) : 0;
}

static void report(const char *who, const struct geom *g)
{
	printf("%s.stackbase=0x%llx\n", who, (unsigned long long)g->base);
	printf("%s.stacklimit=0x%llx\n", who, (unsigned long long)g->limit);
	printf("%s.rsp=0x%llx\n", who, (unsigned long long)g->rsp);
	printf("%s.cygtls_base=0x%llx\n", who, (unsigned long long)g->cygtls);
	printf("%s.owned_gap=%llu\n", who, (unsigned long long)g->gap);
}

/* ---- the alternate signal stack question ------------------------------- */

static uint64_t g_pad;
static struct geom sig_geom;
static volatile sig_atomic_t sig_fired;

static void on_signal(int signo)
{
	(void)signo;
	measure(&sig_geom, g_pad);
	sig_fired = 1;
}

/* Deliver SIGUSR1 with a sigaltstack installed and SA_ONSTACK set, then read
 * the geometry from inside the handler. If Cygwin moved NtTib.StackBase to the
 * alternate stack, base changes and the C3 chain would read the wrong block. */
static void measure_altstack(const struct geom *thread_geom)
{
	stack_t ss;
	struct sigaction sa;
	static char altbuf[SIGSTKSZ > 65536 ? SIGSTKSZ : 65536];

	memset(&ss, 0, sizeof ss);
	ss.ss_sp = altbuf;
	ss.ss_size = sizeof altbuf;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) != 0) {
		printf("altstack.installed=0\n");
		return;
	}
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGUSR1, &sa, NULL);

	sig_fired = 0;
	raise(SIGUSR1);
	while (!sig_fired)
		;

	printf("altstack.installed=1\n");
	printf("altstack.buf=0x%llx..0x%llx\n",
	       (unsigned long long)(uintptr_t)altbuf,
	       (unsigned long long)((uintptr_t)altbuf + sizeof altbuf));
	report("altstack", &sig_geom);
	printf("altstack.base_moved=%d\n", sig_geom.base != thread_geom->base);
	{
		int rsp_on_alt = (uint64_t)(uintptr_t)altbuf <= sig_geom.rsp &&
			sig_geom.rsp < (uint64_t)((uintptr_t)altbuf + sizeof altbuf);
		printf("altstack.rsp_on_altbuf=%d\n", rsp_on_alt);
	}
}

/* ---- a fresh thread ---------------------------------------------------- */

static void *thread_body(void *arg)
{
	struct geom g;
	(void)arg;
	measure(&g, g_pad);
	report("thread", &g);
	measure_altstack(&g);
	return NULL;
}

int main(void)
{
	struct geom main_geom;
	pthread_t th;
	pid_t pid;
	int status;

	g_pad = cygtls_padsize();
	printf("cygtls_padsize=%llu\n", (unsigned long long)g_pad);
	printf("cygtls_padsize_hex=0x%llx\n", (unsigned long long)g_pad);

	measure(&main_geom, g_pad);
	report("main", &main_geom);
	measure_altstack(&main_geom);

	pthread_create(&th, NULL, thread_body, NULL);
	pthread_join(th, NULL);

	/* The fork child: the stand-in's seam was here -- it inherited a nonzero
	 * word where thread start saw a clean zero, because it did not own the
	 * memory. The real _cygtls owns and re-initialises its block across fork,
	 * so the child's geometry is measured here against the real block. */
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		struct geom g;
		measure(&g, g_pad);
		report("fork_child", &g);
		fflush(stdout);
		_exit(0);
	}
	waitpid(pid, &status, 0);

	return 0;
}
