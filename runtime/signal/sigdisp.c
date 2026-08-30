/*
 * sigdisp.c -- the policy around the frame: dispositions, masks, the alternate
 * stack, the flags, and the return.
 *
 * Everything here is POSIX rather than platform. What makes it worth writing
 * out is that the host has its own answers to all of it and none of them are
 * these: Cygwin masks with its own set, keeps its own alternate stack, and
 * decides restart by its own table. An ELF process gets the Linux answers, and
 * this file is where they are given.
 *
 * The return is the sharp end. elfsysv_sigreturn is reached from a handler's
 * own `ret`, which means the frame it is handed has been in the handler's
 * hands, so it is checked before any of it is installed and refused rather
 * than repaired when the check fails. A refused return is the caller's to turn
 * into whatever a kernel would do -- Linux force-delivers SIGSEGV -- and this
 * package reports rather than decides, because the process-level answer is
 * WP-44's and not here.
 */

#include <string.h>

#include "sigpriv.h"

__thread elfsysv_sigstate_t *elfsysv_sig_current;

/* Signals whose default is to be ignored, and the ones that cannot be caught.
 * SIGKILL and SIGSTOP are Linux's 9 and 19. */
static int sig_ignored_by_default(int signo)
{
	return signo == 17	/* SIGCHLD */
	       || signo == 18	/* SIGCONT */
	       || signo == 28	/* SIGWINCH */
	       || signo == 29;	/* SIGURG   */
}

/* The two that may be neither blocked nor caught. */
static uint64_t sig_unblockable(void)
{
	return elf_sigbit(9) | elf_sigbit(19);
}

static int sig_uncatchable(int signo)
{
	return signo == 9 || signo == 19;
}

static int sig_valid(int signo)
{
	return signo >= 1 && signo <= ELFSYSV_NSIG;
}

void elf_sig_init(elfsysv_sigstate_t *st)
{
	memset(st, 0, sizeof(*st));
	st->altstack.ss_flags = ELFSYSV_SS_DISABLE;
	/* The authenticator's secret. Address entropy and the caller's own
	 * state are what is available before the runtime has an RNG; the
	 * cookie's job is to reject bytes a handler wrote, not to withstand an
	 * attacker who can read this word. */
	uint64_t seed = (uint64_t)(uintptr_t)st;
	seed ^= (uint64_t)(uintptr_t)&elfsysv_sig_current << 17;
	seed ^= (uint64_t)(uintptr_t)elfsysv_sig_return_tramp << 31;
	st->cookie = seed | 1;
	st->initialized = 1;
}

int elf_sig_action(elfsysv_sigstate_t *st, int signo,
		   const elfsysv_sigaction_t *act, elfsysv_sigaction_t *old)
{
	if (!sig_valid(signo))
		return -1;
	if (act && sig_uncatchable(signo))
		return -1;
	if (old)
		*old = st->act[signo];
	if (act)
		st->act[signo] = *act;
	return 0;
}

int elf_sig_procmask(elfsysv_sigstate_t *st, int how,
		     const elfsysv_sigset_t *set, elfsysv_sigset_t *old)
{
	uint64_t m;

	if (old)
		elf_sigset_from_mask(old, st->blocked);
	if (!set)
		return 0;
	m = elf_sigset_mask(set);
	/* SIGKILL and SIGSTOP cannot be blocked, and a set that names them is
	 * not an error: the bits are dropped. */
	m &= ~sig_unblockable();
	switch (how) {
	case ELFSYSV_SIG_BLOCK:
		st->blocked |= m;
		break;
	case ELFSYSV_SIG_UNBLOCK:
		st->blocked &= ~m;
		break;
	case ELFSYSV_SIG_SETMASK:
		st->blocked = m;
		break;
	default:
		return -1;
	}
	return 0;
}

int elf_sig_on_altstack(const elfsysv_sigstate_t *st, uintptr_t sp)
{
	uintptr_t base = (uintptr_t)st->altstack.ss_sp;

	if (!base || (st->altstack.ss_flags & ELFSYSV_SS_DISABLE))
		return 0;
	return sp > base && sp <= base + st->altstack.ss_size;
}

int elf_sig_altstack(elfsysv_sigstate_t *st, uintptr_t cur_sp,
		     const elfsysv_stack_t *ss, elfsysv_stack_t *old)
{
	int on = elf_sig_on_altstack(st, cur_sp);

	if (old) {
		*old = st->altstack;
		old->ss_flags = (st->altstack.ss_sp &&
				 !(st->altstack.ss_flags & ELFSYSV_SS_DISABLE))
					? (on ? ELFSYSV_SS_ONSTACK : 0)
					: ELFSYSV_SS_DISABLE;
	}
	if (!ss)
		return 0;
	/* Changing the alternate stack while running on it is EPERM. */
	if (on)
		return -1;
	if (ss->ss_flags & ELFSYSV_SS_DISABLE) {
		st->altstack.ss_sp = 0;
		st->altstack.ss_size = 0;
		st->altstack.ss_flags = ELFSYSV_SS_DISABLE;
		return 0;
	}
	if (ss->ss_flags & ~(ELFSYSV_SS_DISABLE | ELFSYSV_SS_ONSTACK))
		return -1;
	if (ss->ss_size < ELFSYSV_MINSIGSTKSZ ||
	    ss->ss_size < elf_sig_frame_bytes())
		return -1;
	if (!ss->ss_sp)
		return -1;
	st->altstack.ss_sp = ss->ss_sp;
	st->altstack.ss_size = ss->ss_size;
	st->altstack.ss_flags = 0;
	return 0;
}

int elf_sig_restart_after(const elfsysv_sigstate_t *st, int signo)
{
	if (!sig_valid(signo))
		return 0;
	if (st->act[signo].sa_handler == ELFSYSV_SIG_DFL ||
	    st->act[signo].sa_handler == ELFSYSV_SIG_IGN)
		return 1;	/* nothing ran; the call was never interrupted
				 * from the caller's point of view */
	return (st->act[signo].sa_flags & ELFSYSV_SA_RESTART) ? 1 : 0;
}

elf_sig_disposition_t elf_sig_deliver(elfsysv_sigstate_t *st, int signo,
				      const elfsysv_siginfo_t *info,
				      elfsysv_sigctx_t *ctx,
				      elf_sig_placement_t *where)
{
	elf_sig_placement_t local;
	elfsysv_sigaction_t sa;
	uint64_t bit = elf_sigbit(signo);

	if (!where)
		where = &local;
	if (!sig_valid(signo) || !st->initialized)
		return ELF_SIG_REFUSED;

	sa = st->act[signo];

	if (sa.sa_handler == ELFSYSV_SIG_IGN)
		return ELF_SIG_IGNORED;
	if (sa.sa_handler == ELFSYSV_SIG_DFL)
		return sig_ignored_by_default(signo) ? ELF_SIG_IGNORED
						     : ELF_SIG_DEFAULT;
	if ((st->blocked & bit) && !sig_uncatchable(signo))
		return ELF_SIG_BLOCKED;

	if (!elf_sig_place(st, &sa, ctx, where))
		return ELF_SIG_REFUSED;

	uint64_t next = st->blocked | elf_sigset_mask(&sa.sa_mask);
	if (!(sa.sa_flags & ELFSYSV_SA_NODEFER))
		next |= bit;
	next &= ~sig_unblockable();

	elf_sig_build(st, signo, info, &sa, ctx, where, next);

	/* The frame carries the mask that was in force, so the mask may be
	 * advanced only after the frame is written. */
	st->blocked = next;

	if (sa.sa_flags & ELFSYSV_SA_RESETHAND) {
		st->act[signo].sa_handler = ELFSYSV_SIG_DFL;
		st->act[signo].sa_flags &= ~ELFSYSV_SA_RESETHAND;
	}
	return ELF_SIG_DELIVERED;
}

/* The return. Reached from the trampoline with the frame address; installs the
 * saved state and does not come back. */
ELF_SYSV int elfsysv_sigreturn(uintptr_t frame)
{
	elfsysv_sigstate_t *st = elfsysv_sig_current;
	char why[ELF_SIG_WHY_MAX];

	if (!st)
		return -1;

	/* What is readable is what the frame itself claims, and the claim is
	 * authenticated before it is used as a bound; the check re-derives it
	 * from the same bytes rather than taking this one on trust. */
	const elfsysv_sigframe_t *f = (const elfsysv_sigframe_t *)frame;
	uint64_t top = (uint64_t)f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP];
	size_t len = (top > (uint64_t)frame && top - (uint64_t)frame < (1u << 20))
			     ? (size_t)(top - (uint64_t)frame)
			     : sizeof(elfsysv_sigframe_t);

	if (!elf_sigframe_check(st, frame, len, why))
		return -1;

	elfsysv_greg_t g[ELF_NGREG];
	memcpy(g, f->uc.uc_mcontext.gregs, sizeof(g));

	/* Only the flags a user may set travel back. */
	g[ELF_REG_EFL] = (elfsysv_greg_t)(((uint64_t)g[ELF_REG_EFL] &
					   ELFSYSV_EFLAGS_MASK) |
					  ELFSYSV_EFLAGS_FIXED);

	st->blocked = elf_sigset_mask(&f->uc.uc_sigmask) & ~sig_unblockable();

	elf_sig_fprestore(f->uc.uc_mcontext.fpregs, f->uc.uc_flags);

	elfsysv_sig_restore(g);
}
