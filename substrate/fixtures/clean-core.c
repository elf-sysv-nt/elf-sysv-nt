/*
 * clean-core.c -- a fixture, not a build input.  It is shaped like a core
 * source that stays above the substrate line: it names threads by tid and
 * ranges by address, reaches user memory only through user_copy_*, and hands
 * whole register states across in Linux terms.  check-substrate-line finds no
 * leak here and exits 0.
 */
#include "substrate.h"

/* Deliver a signal to a stopped thread by building the frame in user memory and
 * handing the substrate a whole new register state -- no host primitive named. */
int deliver_signal(struct substrate *s, int tid, const void *frame,
		   uint64_t frame_uaddr, size_t frame_len,
		   const struct sub_regs *resume)
{
	uint64_t fault = 0;

	if (s->thread_interrupt(s, tid))
		return -1;
	/* The core writes the frame through the copy call; it never touches the
	 * user address itself. */
	if (s->user_copy_out(s, frame_uaddr, frame, frame_len, &fault))
		return -1;
	if (s->set_context(s, tid, resume))
		return -1;
	return s->thread_start(s, tid, resume, resume->fs_base);
}
