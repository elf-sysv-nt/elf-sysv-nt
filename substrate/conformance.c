/*
 * conformance.c -- the substrate conformance suite, one group per the spec's
 * nine numbered conformance bars (doc/design/Substrate-Interface.md,
 * "Conformance").  It runs against whatever vtable substrate_create() hands it,
 * so N and H reuse it unchanged once they replace the mock; a group that needed
 * editing to admit a substrate would be measuring mechanism, not the contract,
 * and the spec forbids that.
 *
 * Each group asserts the contract, not the mock's mechanism.  The suite drives
 * user code the way the core would -- register states pointing at small probe
 * functions, stacks and buffers reached only through as_map and the user
 * copies -- and reads results back through the same interface, so nothing here
 * names a host primitive.  The three probes below are the "user code" the core
 * would run; they speak to the suite through file-scope words rather than
 * arguments, which keeps the register state the suite builds down to an rip and
 * an rsp, and sidesteps the calling convention entirely.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "substrate.h"

#define PAGE 4096u

/* A landmark the thread_start and context groups watch for. */
#define LANDMARK      0xC0FFEE5117ULL
/* A thread pointer the tp_set group plants and reads back. */
#define TP_MAGIC      0x5EED1234ABCD0000ULL

static void relax(void) { __builtin_ia32_pause(); }

/* Wait for a word to reach a value, bounded so a wedged run fails rather than
 * hangs.  Returns 0 if it arrived, -1 if the bound ran out. */
static int wait_eq(volatile int *w, int want)
{
	long i;

	for (i = 0; i < 500000000L; i++) {
		if (*w == want)
			return 0;
		relax();
	}
	return -1;
}

/* ---- the probes (the "user code" the core runs) ------------------------- */

static volatile int      reached;		/* thread_start / rip-rewrite */
static volatile uint64_t reached_val;

static volatile int      spin_running;		/* the interrupt-group spinner */
static volatile int      spin_stop;
static volatile int      spin_left;		/* incremented as it exits */
static volatile uint64_t spin_count;

static volatile int      tp_ready;		/* tp_set probe handshake */
static volatile int      tp_go;
static volatile int      tp_done;
static volatile uint64_t tp_readback;

__attribute__((noinline, used))
static void landmark_probe(void)
{
	reached_val = LANDMARK;
	reached = 1;
}

__attribute__((noinline, used))
static void spin_probe(void)
{
	spin_running = 1;
	while (!spin_stop) {
		spin_count++;
		relax();
	}
	spin_left++;
}

/* Set rip here on a resume and the thread parks rather than returning into a
 * stack the resume path left untouched; the rip-rewrite check only needs the
 * landmark to be reached. */
__attribute__((noinline, used))
static void park_probe(void)
{
	reached_val = LANDMARK;
	reached = 1;
	for (;;)
		relax();
}

__attribute__((noinline, used))
static void tp_probe(void)
{
	tp_ready = 1;
	while (!tp_go)			/* the wait is the deschedule the */
		relax();		/* runtime word must survive */
	tp_readback = substrate_thread_pointer();
	tp_done = 1;
}

/* ---- register-state and stack helpers ----------------------------------- */

/* A fresh user stack, and the 16-aligned top a fresh thread_start wants. */
static uint64_t new_stack(struct substrate *s, uint64_t *top)
{
	void *base = NULL;
	struct sub_backing b = { .kind = SUB_BACKING_STACK };
	size_t len = 512 * 1024;

	if (s->as_map(s, &base, len, &b, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	/* Leave a little headroom below the very top. */
	*top = ((uint64_t)base + len - 64) & ~(uint64_t)15;
	return (uint64_t)base;
}

static void regs_at(struct sub_regs *r, void (*fn)(void), uint64_t sp)
{
	memset(r, 0, sizeof *r);
	r->rip = (uint64_t)(uintptr_t)fn;
	r->rsp = sp;
}

/* ---- group 1: as_map ---------------------------------------------------- */

static int grp_as_map(struct substrate *s)
{
	struct sub_backing anon = { .kind = SUB_BACKING_ANON };
	static const unsigned char file[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	};
	struct sub_backing fb = {
		.kind = SUB_BACKING_FILE, .file_image = file, .file_len = 16
	};
	void *a = NULL, *b = NULL, *c = NULL;
	unsigned char buf[32];
	uint64_t fault = 0;
	int ok = 1, i;

	/* A mapped anonymous page reads zero on first touch. */
	if (s->as_map(s, &a, PAGE, &anon, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	memset(buf, 0xAB, sizeof buf);
	if (s->user_copy_in(s, buf, (uint64_t)a, sizeof buf, &fault))
		ok = 0;
	for (i = 0; i < (int)sizeof buf; i++)
		if (buf[i] != 0)
			ok = 0;

	/* A file backing reads the file at offset. */
	if (s->as_map(s, &b, PAGE, &fb, 4, SUB_PROT_READ | SUB_PROT_WRITE))
		ok = 0;
	else {
		unsigned char rb[8];

		if (s->user_copy_in(s, rb, (uint64_t)b, 8, &fault))
			ok = 0;
		for (i = 0; i < 8; i++)
			if (rb[i] != file[4 + i])
				ok = 0;
	}

	/* A failed map leaves nothing mapped: 64 TiB commits nowhere. */
	if (s->as_map(s, &c, (size_t)1 << 46, &anon, 0,
		      SUB_PROT_READ | SUB_PROT_WRITE) == 0)
		ok = 0;

	return ok;
}

/* ---- group 2: as_unmap -------------------------------------------------- */

static int grp_as_unmap(struct substrate *s)
{
	void *base = NULL;
	uint64_t p0, p1, p2, fault = 0;
	unsigned char s0[8], s1[8], s2[8], rb[8];
	int ok = 1, i;

	if (s->as_map(s, &base, 3 * PAGE, &(struct sub_backing){ .kind =
		      SUB_BACKING_ANON }, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	p0 = (uint64_t)base;
	p1 = p0 + PAGE;
	p2 = p0 + 2 * PAGE;

	memset(s0, 0xA0, 8); memset(s1, 0xB1, 8); memset(s2, 0xC2, 8);
	if (s->user_copy_out(s, p0, s0, 8, &fault)) ok = 0;
	if (s->user_copy_out(s, p1, s1, 8, &fault)) ok = 0;
	if (s->user_copy_out(s, p2, s2, 8, &fault)) ok = 0;

	/* Drop the middle page only. */
	if (s->as_unmap(s, (void *)(uintptr_t)p1, PAGE))
		ok = 0;

	/* The dropped range faults. */
	fault = 0;
	if (s->user_copy_in(s, rb, p1, 8, &fault) != -1)
		ok = 0;
	if (!(fault >= p1 && fault < p1 + PAGE))
		ok = 0;

	/* The neighbours are intact to the byte. */
	if (s->user_copy_in(s, rb, p0, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++) if (rb[i] != s0[i]) ok = 0;
	if (s->user_copy_in(s, rb, p2, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++) if (rb[i] != s2[i]) ok = 0;

	return ok;
}

/* ---- group 3: as_protect ------------------------------------------------ */

static int grp_as_protect(struct substrate *s)
{
	void *p = NULL;
	uint64_t u, fault = 0;
	unsigned char sent[16], rb[16];
	int ok = 1, i;

	if (s->as_map(s, &p, PAGE, &(struct sub_backing){ .kind =
		      SUB_BACKING_ANON }, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	u = (uint64_t)p;
	for (i = 0; i < 16; i++) sent[i] = (unsigned char)(0x30 + i);
	if (s->user_copy_out(s, u, sent, 16, &fault)) ok = 0;

	/* Drop it to read-only. */
	if (s->as_protect(s, p, PAGE, SUB_PROT_READ)) ok = 0;

	/* A write now faults. */
	fault = 0;
	if (s->user_copy_out(s, u, sent, 16, &fault) != -1) ok = 0;
	if (!(fault >= u && fault < u + PAGE)) ok = 0;

	/* And the contents survived the change. */
	if (s->user_copy_in(s, rb, u, 16, &fault)) ok = 0;
	for (i = 0; i < 16; i++) if (rb[i] != sent[i]) ok = 0;

	return ok;
}

/* ---- group 4: as_clone -------------------------------------------------- */

/*
 * A cross-process substrate (N's RtlCloneUserProcess) produces a child that is
 * a separate process, so the parent cannot reach into it with the child
 * substrate's user copies the way it can an in-process copy (the mock).  The
 * contract is unchanged -- the child reads the parent's pre-clone contents, and
 * a later write on either side stays private -- but the driving differs: the
 * parent leaves the address and the two patterns where the clone carries them,
 * the child certifies on its own pages, and the parent reads the verdict from
 * the child's exit status.  This block is reached only when the substrate
 * declares clone_cross_process; the mock keeps the in-process path below it.
 */
static struct {
	uint64_t uaddr;
	unsigned char pre[8];
	unsigned char child_val[8];
} clone_plan;

/* Runs on the cloned thread inside the child.  It reads the parent's pre-clone
 * bytes and writes a value of its own, then confirms that write, touching its
 * page directly: a fresh clone must not reach the heap or a service, and the
 * page is known mapped, so no fault guard is owed.  A nonzero return is the
 * child's exit code and says which half failed. */
static int clone_child_certify(struct substrate *s)
{
	volatile unsigned char *p =
		(volatile unsigned char *)(uintptr_t)clone_plan.uaddr;
	int i;

	(void)s;
	for (i = 0; i < 8; i++)
		if (p[i] != clone_plan.pre[i])
			return 11;	/* the address space did not clone */
	for (i = 0; i < 8; i++)
		p[i] = clone_plan.child_val[i];
	for (i = 0; i < 8; i++)
		if (p[i] != clone_plan.child_val[i])
			return 12;	/* the child's own write did not hold */
	return 0;
}

static int as_clone_cross(struct substrate *s, uint64_t u,
			  const unsigned char *pre)
{
	struct substrate *child = NULL;
	unsigned char child_val[8], par_val[8], rb[8];
	uint64_t fault = 0;
	int verdict, i, ok = 1;

	memset(child_val, 0x22, 8);
	memset(par_val, 0x33, 8);
	clone_plan.uaddr = u;
	memcpy(clone_plan.pre, pre, 8);
	memcpy(clone_plan.child_val, child_val, 8);
	s->clone_child_certify = clone_child_certify;

	if (s->as_clone(s, &child) || !child)
		return 0;

	/* The child ran its certification and exited with the verdict. */
	verdict = child->clone_wait ? child->clone_wait(child) : -1;

	/* The parent's own post-clone write is private to the parent: it still
	 * reads its own value, never the child's. */
	if (s->user_copy_out(s, u, par_val, 8, &fault)) ok = 0;
	if (s->user_copy_in(s, rb, u, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++)
		if (rb[i] != par_val[i])
			ok = 0;

	return ok && verdict == 0;
}

static int grp_as_clone(struct substrate *s)
{
	struct substrate *child = NULL;
	void *p = NULL;
	uint64_t u, fault = 0;
	unsigned char pre[8], s2[8], s3[8], rb[8];
	int ok = 1, i;

	if (s->as_map(s, &p, PAGE, &(struct sub_backing){ .kind =
		      SUB_BACKING_ANON }, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	u = (uint64_t)p;
	memset(pre, 0x11, 8);
	if (s->user_copy_out(s, u, pre, 8, &fault)) ok = 0;

	/* A clone that crosses a process boundary is certified by the child. */
	if (s->clone_cross_process)
		return ok && as_clone_cross(s, u, pre);

	if (s->as_clone(s, &child) || !child)
		return 0;

	/* The child reads the parent's pre-clone contents. */
	if (child->user_copy_in(child, rb, u, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++) if (rb[i] != pre[i]) ok = 0;

	/* A post-clone write on either side is private to it. */
	memset(s2, 0x22, 8); memset(s3, 0x33, 8);
	if (child->user_copy_out(child, u, s2, 8, &fault)) ok = 0;
	if (s->user_copy_out(s, u, s3, 8, &fault)) ok = 0;

	if (s->user_copy_in(s, rb, u, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++) if (rb[i] != s3[i]) ok = 0;
	if (child->user_copy_in(child, rb, u, 8, &fault)) ok = 0;
	for (i = 0; i < 8; i++) if (rb[i] != s2[i]) ok = 0;

	return ok;
}

/* ---- thread helpers ----------------------------------------------------- */

static int next_tid = 1000;

/* Wait for spin_count to climb past a mark; 0 if it did, -1 on the bound. */
static int wait_climb(uint64_t mark)
{
	long i;

	for (i = 0; i < 500000000L; i++) {
		if (spin_count > mark)
			return 0;
		relax();
	}
	return -1;
}

static int start_spin(struct substrate *s, int *tid_out)
{
	struct sub_regs r;
	uint64_t top;
	int tid = next_tid++;

	spin_running = 0;
	spin_stop = 0;
	spin_count = 0;
	if (!new_stack(s, &top))
		return -1;
	regs_at(&r, spin_probe, top);
	if (s->thread_start(s, tid, &r, 0))
		return -1;
	if (wait_eq((volatile int *)&spin_running, 1))
		return -1;
	*tid_out = tid;
	return 0;
}

/* Only call on a running (resumed) spinner: a suspended one never sees stop. */
static void stop_spin(void)
{
	int base = spin_left;

	spin_stop = 1;
	wait_eq(&spin_left, base + 1);
}

/* ---- group 5: thread_start ---------------------------------------------- */

static int grp_thread_start(struct substrate *s)
{
	struct sub_regs r;
	uint64_t top;
	int tid = next_tid++;

	reached = 0;
	reached_val = 0;
	if (!new_stack(s, &top))
		return 0;
	regs_at(&r, landmark_probe, top);
	if (s->thread_start(s, tid, &r, 0))
		return 0;
	if (wait_eq((volatile int *)&reached, 1))
		return 0;
	return reached_val == LANDMARK;
}

/* ---- group 6: thread_interrupt ------------------------------------------ */

static int grp_thread_interrupt(struct substrate *s)
{
	struct sub_regs saved, nc, chk;
	unsigned char before[128], after[128];
	uint64_t f1, f2, mark, R, fault = 0;
	int tid, a_ok, b_ok, c_ok, k;
	long i;

	/* (a) a spinning thread is forced out and resumes intact. */
	if (start_spin(s, &tid))
		return 0;
	mark = spin_count;
	if (wait_climb(mark))
		return 0;
	if (s->thread_interrupt(s, tid))
		return 0;
	if (s->thread_context(s, tid, &saved))	/* also settles the stop */
		return 0;
	f1 = spin_count;
	for (i = 0; i < 4000000L; i++)
		relax();
	f2 = spin_count;
	if (s->thread_start(s, tid, &saved, 0))	/* resume from saved state */
		return 0;
	a_ok = (f1 == f2) && (wait_climb(f2 + 8) == 0);	/* frozen, then runs */
	stop_spin();

	/* (b) an interrupt during a non-interruptible window is latched once. */
	if (start_spin(s, &tid))
		return 0;
	if (substrate_gate_enter(s, tid))
		return 0;
	mark = spin_count;
	for (k = 0; k < 5; k++)
		s->thread_interrupt(s, tid);	/* five, all latched */
	b_ok = (wait_climb(mark + 8) == 0);	/* it kept running, not forced out */
	b_ok = b_ok && (substrate_gate_exit(s, tid) == 1);	/* taken once */
	b_ok = b_ok && (substrate_gate_exit(s, tid) == 0);	/* nothing left */
	stop_spin();

	/* (c) the red zone 128 below the interrupted rsp survives a set_context. */
	if (start_spin(s, &tid))
		return 0;
	if (s->thread_interrupt(s, tid))
		return 0;
	if (s->thread_context(s, tid, &saved))
		return 0;
	R = saved.rsp;
	if (s->user_copy_in(s, before, R - 128, 128, &fault))
		return 0;
	nc = saved;
	nc.rsp = R - 192;			/* core reserved 128, then a frame */
	if (s->set_context(s, tid, &nc))
		return 0;
	if (s->user_copy_in(s, after, R - 128, 128, &fault))
		return 0;
	c_ok = memcmp(before, after, 128) == 0;
	if (s->thread_context(s, tid, &chk))
		return 0;
	c_ok = c_ok && (chk.rsp == R - 192);	/* the reserved rsp was honoured */
	if (s->thread_start(s, tid, &saved, 0))	/* resume clean, then stop */
		return 0;
	stop_spin();

	return a_ok && b_ok && c_ok;
}

/* ---- group 7: thread_context / set_context ------------------------------ */

static int grp_thread_context(struct substrate *s)
{
	struct sub_regs saved, mod, rb, go;
	int tid, regs_ok, rip_ok;

	if (start_spin(s, &tid))
		return 0;
	if (s->thread_interrupt(s, tid))
		return 0;
	if (s->thread_context(s, tid, &saved))
		return 0;

	/* A round-trip preserves every register it was handed. */
	mod = saved;
	mod.r12 = 0x1212121212121212ULL;
	mod.r13 = 0x1313131313131313ULL;
	mod.r14 = 0x1414141414141414ULL;
	mod.r15 = 0x1515151515151515ULL;
	mod.rbx = 0xB0B0B0B0B0B0B0B0ULL;
	if (s->set_context(s, tid, &mod))
		return 0;
	if (s->thread_context(s, tid, &rb))
		return 0;
	regs_ok = rb.r12 == mod.r12 && rb.r13 == mod.r13 && rb.r14 == mod.r14 &&
		  rb.r15 == mod.r15 && rb.rbx == mod.rbx;

	/* A rewritten rip takes effect on resume. */
	reached = 0;
	reached_val = 0;
	go = saved;
	go.rip = (uint64_t)(uintptr_t)park_probe;
	if (s->set_context(s, tid, &go))
		return 0;
	if (s->thread_start(s, tid, &go, 0))	/* resume at the new rip */
		return 0;
	rip_ok = (wait_eq((volatile int *)&reached, 1) == 0) &&
		 reached_val == LANDMARK;
	/* park_probe does not spin on spin_stop; leave it parked (reaped at
	 * process exit) rather than reaching for a host thread-kill here. */

	return regs_ok && rip_ok;
}

/* ---- group 8: tp_set ---------------------------------------------------- */

static int grp_tp_set(struct substrate *s)
{
	struct sub_regs r;
	uint64_t top;
	int tid = next_tid++;

	tp_ready = 0;
	tp_go = 0;
	tp_done = 0;
	tp_readback = 0;
	if (!new_stack(s, &top))
		return 0;
	regs_at(&r, tp_probe, top);
	if (s->thread_start(s, tid, &r, 0))
		return 0;
	if (wait_eq((volatile int *)&tp_ready, 1))
		return 0;
	if (s->tp_set(s, tid, TP_MAGIC))
		return 0;
	tp_go = 1;
	if (wait_eq((volatile int *)&tp_done, 1))
		return 0;
	return tp_readback == TP_MAGIC;
}

/* ---- group 9: user_copy ------------------------------------------------- */

static int grp_user_copy(struct substrate *s)
{
	struct sub_backing anon = { .kind = SUB_BACKING_ANON };
	void *p = NULL, *q = NULL;
	uint64_t u, bad, fault = 0;
	unsigned char src[16], rb[16];
	int ok = 1, i, rc;

	if (s->as_map(s, &p, PAGE, &anon, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	u = (uint64_t)p;
	for (i = 0; i < 16; i++)
		src[i] = (unsigned char)(0x40 + i);

	/* A good copy moves the bytes, both directions. */
	if (s->user_copy_out(s, u, src, 16, &fault)) ok = 0;
	if (s->user_copy_in(s, rb, u, 16, &fault)) ok = 0;
	for (i = 0; i < 16; i++) if (rb[i] != src[i]) ok = 0;

	/* A copy to an unmapped user address fails with the fault address and
	 * does not crash: map a page, drop it, then reach for it. */
	if (s->as_map(s, &q, PAGE, &anon, 0, SUB_PROT_READ | SUB_PROT_WRITE))
		return 0;
	bad = (uint64_t)q;
	if (s->as_unmap(s, q, PAGE)) ok = 0;
	fault = 0;
	rc = s->user_copy_in(s, rb, bad, 16, &fault);
	if (!(rc == -1 && fault >= bad && fault < bad + PAGE)) ok = 0;

	return ok;
}

/* ---- the driver --------------------------------------------------------- */

#define CONFORMANCE_VERSION "conformance 1.0"

struct group {
	const char *name;
	int (*fn)(struct substrate *s);
};

static const struct group groups[] = {
	{ "as_map",           grp_as_map },
	{ "as_unmap",         grp_as_unmap },
	{ "as_protect",       grp_as_protect },
	{ "as_clone",         grp_as_clone },
	{ "thread_start",     grp_thread_start },
	{ "thread_interrupt", grp_thread_interrupt },
	{ "thread_context",   grp_thread_context },
	{ "tp_set",           grp_tp_set },
	{ "user_copy",        grp_user_copy },
};

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  conformance [options]\n"
	      "\n"
	      "Run the substrate conformance suite against the linked substrate,\n"
	      "one group per the nine conformance bars.\n"
	      "\n"
	      "Options:\n"
	      "  -q, --quiet     Print failing groups and the summary only.\n"
	      "  -V, --version   Print the version and exit.\n"
	      "  -h, --help      Print this message and exit.\n"
	      "\n"
	      "Exit: 0 every group passes, 1 a group fails, 2 a usage error.\n",
	      out);
}

int main(int argc, char **argv)
{
	int quiet = 0, i, npass = 0;
	int n = (int)(sizeof groups / sizeof groups[0]);
	struct substrate *s;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			puts(CONFORMANCE_VERSION);
			return 0;
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			quiet = 1;
		} else {
			fprintf(stderr, "conformance: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
	}

	s = substrate_create();
	if (!s) {
		fprintf(stderr, "conformance: could not create a substrate\n");
		return 2;
	}

	for (i = 0; i < n; i++) {
		int ok = groups[i].fn(s);

		if (ok)
			npass++;
		if (ok && quiet)
			continue;
		printf("group=%s %s\n", groups[i].name, ok ? "pass" : "FAIL");
	}
	printf("summary=%d/%d groups pass\n", npass, n);
	printf("verdict=%s\n", npass == n ? "pass" : "FAIL");
	return npass == n ? 0 : 1;
}
