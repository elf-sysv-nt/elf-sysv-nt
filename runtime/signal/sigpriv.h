/*
 * sigpriv.h -- what the three files of this package say to each other.
 *
 * The reserved words of mcontext_t carry the package's own bookkeeping. Linux
 * zeroes __reserved1 and no consumer reads it, and a return path needs two
 * facts about a frame that the frame itself must carry: how far it extends,
 * so the extended FPU area can be bounded before it is read, and whether the
 * bytes are the ones the placer wrote. Both are authenticated together, since
 * an extent a handler can rewrite is not a bound.
 */

#ifndef ELFSYSV_RUNTIME_SIGPRIV_H
#define ELFSYSV_RUNTIME_SIGPRIV_H

#include "signal.h"

#define ELF_SIG_RSV_COOKIE 0		/* authenticator over frame and top  */
#define ELF_SIG_RSV_TOP 1		/* one past the highest byte written */
#define ELF_SIG_RSV_PLACE 2		/* ELF_SIG_PLACED_* below            */

#define ELF_SIG_PLACED_ALTSTACK 0x1
#define ELF_SIG_PLACED_RESERVED 0x2	/* the red zone was skipped          */

/* Headroom kept below the frame. The return trampoline steps rsp below the
 * frame before it calls, so the pushed return address does not land in bytes
 * the check is about to read; on an alternate stack that step has to stay
 * inside the stack the caller gave us. */
#define ELF_SIG_HEADROOM 128

/* An address a user-mode thread on this platform could plausibly run at. */
static inline int elf_sig_canonical(uint64_t v)
{
	uint64_t top = v >> 47;
	return top == 0 || top == 0x1ffff;
}

/* Save the calling thread's extended FPU state into a placed area, or copy the
 * fxsave image the hijack captured. Returns the bytes written. */
size_t elf_sig_fpsave(void *area, const void *fxsave);

/* Install an extended FPU state that has already been checked. */
void elf_sig_fprestore(const elfsysv_fpstate_t *fp, uint64_t uc_flags);

/* Where the frame goes for this delivery. Fills `where` and returns 1, or
 * returns 0 when the stack on offer cannot hold one. */
int elf_sig_place(const elfsysv_sigstate_t *st, const elfsysv_sigaction_t *sa,
		  const elfsysv_sigctx_t *ctx, elf_sig_placement_t *where);

/* Write the frame at the placed addresses and rewrite `ctx` to enter the
 * handler. Everything decided; this only writes. */
void elf_sig_build(const elfsysv_sigstate_t *st, int signo,
		   const elfsysv_siginfo_t *info,
		   const elfsysv_sigaction_t *sa, elfsysv_sigctx_t *ctx,
		   const elf_sig_placement_t *where, uint64_t new_mask);

#endif /* ELFSYSV_RUNTIME_SIGPRIV_H */
