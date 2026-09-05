/*
 * gate.c -- the C side of the gate.  gate_entry (gate.S) does the register and
 * stack work; here the dispatch happens, bracketed by the substrate's gate
 * window so an interrupt that arrives mid-syscall is latched and taken once on
 * the way out, exactly as the interface promises.  Phase 1 raises no interrupts,
 * but the bracket is the real mechanism and costs nothing to keep honest.
 */
#include "gate.h"
#include "syscall.h"

/*
 * The kernel stack top, read straight from gate.S by RIP-relative name.  One
 * user thread in this increment means one kernel stack; a per-thread carrier is
 * the multi-thread follow-up, not this bar.  Defined here so the asm and the C
 * agree on the symbol.
 */
void *g_kstack_top;

static struct substrate *g_sub;
static int g_tid;

void gate_init(struct substrate *s, int tid, void *kstack_top)
{
	g_sub = s;
	g_tid = tid;
	g_kstack_top = kstack_top;
}

long gate_dispatch(const struct sysframe *f)
{
	long r;

	/* Enter the gate window: the thread is in the kernel path now, so an
	 * interrupt aimed at it latches rather than lands. */
	substrate_gate_enter(g_sub, g_tid);
	r = syscall_dispatch(g_sub, f);
	/* exit_group never returns here; for everything else, close the window
	 * and take a latched interrupt if one waited. */
	substrate_gate_exit(g_sub, g_tid);
	return r;
}
