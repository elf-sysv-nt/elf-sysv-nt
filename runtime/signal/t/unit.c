/*
 * unit.c -- the delivery held to its arithmetic, its POSIX, and one real
 * round trip.
 *
 * Three groups. The first is the shape of what a compiled consumer sees: the
 * sizes and offsets a handler built against Linux headers computes for itself,
 * checked here rather than described, since getting one of them wrong is a
 * defect no test of behaviour would catch.
 *
 * The second is policy: masks, the alternate stack, and what each SA_ flag
 * means. These need no stack of their own and no delivery, only the state.
 *
 * The third is a delivery that actually happens. sig_redzone_trial runs the
 * interrupted side on a stack of its own so the bytes below its stack pointer
 * can be painted and read back, and the whole path runs: placement, frame,
 * handler, trampoline, check, restore, iretq.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sigpriv.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("not ok - %s\n", what);
	}
}

/* ---- group one: the shape ---------------------------------------------- */

static void shape(void)
{
	ok(sizeof(elfsysv_siginfo_t) == 128, "siginfo_t is 128 bytes");
	ok(sizeof(elfsysv_fpstate_t) == 512, "fpstate is 512 bytes");
	ok(sizeof(elfsysv_mcontext_t) == 256, "mcontext_t is 256 bytes");
	ok(offsetof(elfsysv_ucontext_t, uc_mcontext) == 40,
	   "uc_mcontext is at 40");
	ok(offsetof(elfsysv_ucontext_t, uc_mcontext.gregs) +
		   ELF_REG_RSP * 8 == 160,
	   "the interrupted rsp is at ucontext+160");
	ok(offsetof(elfsysv_sigframe_t, uc) == 8,
	   "ucontext follows the return address");
	ok(offsetof(elfsysv_sigframe_t, info) == 8 + sizeof(elfsysv_ucontext_t),
	   "siginfo follows the ucontext");
	ok(ELF_NGREG == 23, "there are 23 general registers in the frame");
}

/* ---- group two: policy -------------------------------------------------- */

static void mask_and_altstack(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigset_t s, old;
	elfsysv_stack_t ss, oldss;
	static char region[64 * 1024];

	elf_sig_init(&st);

	elf_sigset_from_mask(&s, elf_sigbit(10) | elf_sigbit(9));
	ok(elf_sig_procmask(&st, ELFSYSV_SIG_BLOCK, &s, NULL) == 0,
	   "blocking a set succeeds");
	ok(elf_sig_tls(&st)->blocked == elf_sigbit(10),
	   "SIGKILL is dropped from a blocked set rather than refused");

	elf_sig_procmask(&st, ELFSYSV_SIG_SETMASK, NULL, &old);
	ok(elf_sigset_mask(&old) == elf_sigbit(10),
	   "the old mask reads back");
	ok(elf_sig_procmask(&st, 99, &s, NULL) == -1,
	   "an unknown how is refused");

	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = region;
	ss.ss_size = 64;
	ok(elf_sig_altstack(&st, 0, &ss, NULL) == -1,
	   "an alternate stack below MINSIGSTKSZ is refused");

	ss.ss_size = sizeof(region);
	ok(elf_sig_altstack(&st, 0, &ss, &oldss) == 0,
	   "a large enough alternate stack is accepted");
	ok(oldss.ss_flags == ELFSYSV_SS_DISABLE,
	   "the previous alternate stack reads back disabled");

	uintptr_t inside = (uintptr_t)region + 1024;
	ok(elf_sig_on_altstack(&st, inside) == 1,
	   "a stack pointer inside the region is on the alternate stack");
	ok(elf_sig_on_altstack(&st, (uintptr_t)region) == 0,
	   "the base itself is not on it");
	ok(elf_sig_altstack(&st, inside, &ss, NULL) == -1,
	   "changing the alternate stack while on it is refused");

	elf_sig_altstack(&st, 0, NULL, &oldss);
	ok(oldss.ss_flags == 0, "an enabled stack reads back not on it");
	elf_sig_altstack(&st, inside, NULL, &oldss);
	ok(oldss.ss_flags == ELFSYSV_SS_ONSTACK,
	   "asking from on it reads back SS_ONSTACK");
}

static void handler_stub(int signo)
{
	(void)signo;
}

static void flags(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa, old;

	elf_sig_init(&st);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)handler_stub;

	ok(elf_sig_action(&st, 9, &sa, NULL) == -1,
	   "SIGKILL cannot be caught");
	ok(elf_sig_action(&st, 0, &sa, NULL) == -1,
	   "signal zero is not a signal");
	ok(elf_sig_action(&st, 65, &sa, NULL) == -1,
	   "signal 65 is out of range");

	sa.sa_flags = ELFSYSV_SA_RESTART;
	ok(elf_sig_action(&st, 10, &sa, &old) == 0, "a handler installs");
	ok(old.sa_handler == ELFSYSV_SIG_DFL,
	   "the previous disposition reads back SIG_DFL");
	ok(elf_sig_restart_after(&st, 10) == 1,
	   "SA_RESTART restarts an interrupted call");

	sa.sa_flags = 0;
	elf_sig_action(&st, 11, &sa, NULL);
	ok(elf_sig_restart_after(&st, 11) == 0,
	   "without SA_RESTART the call fails EINTR");

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = ELFSYSV_SIG_IGN;
	elf_sig_action(&st, 12, &sa, NULL);
	ok(elf_sig_restart_after(&st, 12) == 1,
	   "an ignored signal never interrupted the call at all");
}

/* ---- placement: the DR-0006 arithmetic --------------------------------- */

static void placement(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa;
	elfsysv_sigctx_t ctx;
	elf_sig_placement_t w;
	static char region[64 * 1024];
	elfsysv_stack_t ss;

	elf_sig_init(&st);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)handler_stub;
	memset(&ctx, 0, sizeof(ctx));

	/* A plausible interrupted stack pointer, on a real stack so the
	 * arithmetic has something to be relative to. */
	uintptr_t sp = (uintptr_t)region + sizeof(region) - 4096;
	ctx.rsp = sp;

	ok(elf_sig_place(&st, &sa, &ctx, &w) == 1, "a frame places");
	ok(w.reserved == ELFSYSV_REDZONE,
	   "the placer skipped exactly 128 bytes");
	ok(w.top == sp - ELFSYSV_REDZONE,
	   "the highest byte written is 128 below the interrupted rsp");
	ok(w.frame < w.top, "the frame is below its own top");
	ok((w.frame & 0xf) == 8,
	   "the handler enters with rsp%16 == 8, as after a call");
	ok((w.fpstate & 0x3f) == 0, "the extended area is 64-byte aligned");
	ok(w.fpstate >= w.frame + sizeof(elfsysv_sigframe_t),
	   "the extended area is above the frame proper");
	ok(w.fpstate + w.fpstate_len <= w.top,
	   "the extended area fits under the reservation");
	ok(w.on_altstack == 0, "this one is not on an alternate stack");

	/* With SA_ONSTACK and a stack to use, the reservation is not needed:
	 * the delivery is not building on the interrupted stack at all. */
	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = region;
	ss.ss_size = 32 * 1024;
	ok(elf_sig_altstack(&st, 0, &ss, NULL) == 0, "the alternate installs");
	sa.sa_flags = ELFSYSV_SA_ONSTACK;
	ok(elf_sig_place(&st, &sa, &ctx, &w) == 1, "an alternate frame places");
	ok(w.on_altstack == 1, "and says so");
	ok(w.reserved == 0, "with no reservation, having not built there");
	ok(w.top == (uintptr_t)region + 32 * 1024,
	   "the alternate frame starts at the top of the region");
	ok(w.frame >= (uintptr_t)region + ELF_SIG_HEADROOM,
	   "and leaves headroom below itself");

	/* A stack pointer already inside the alternate stack does not nest. */
	ctx.rsp = (uintptr_t)region + 16 * 1024;
	ok(elf_sig_place(&st, &sa, &ctx, &w) == 1, "a nested frame places");
	ok(w.on_altstack == 0, "on the current stack, not the alternate again");
	ok(w.reserved == ELFSYSV_REDZONE, "reserving the red zone as usual");
}

/* ---- dispositions ------------------------------------------------------- */

static void dispositions(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa;
	elfsysv_sigctx_t ctx;
	elf_sig_placement_t w;
	static char region[64 * 1024];
	uintptr_t sp = (uintptr_t)region + sizeof(region) - 4096;

	elf_sig_init(&st);
	memset(&ctx, 0, sizeof(ctx));
	ctx.rsp = sp;

	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_DEFAULT,
	   "an uncaught catchable signal is the caller's default action");
	ok(elf_sig_deliver(&st, 17, NULL, &ctx, &w) == ELF_SIG_IGNORED,
	   "SIGCHLD with no handler is ignored");

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = ELFSYSV_SIG_IGN;
	elf_sig_action(&st, 10, &sa, NULL);
	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_IGNORED,
	   "SIG_IGN ignores");

	sa.sa_handler = (uintptr_t)handler_stub;
	sa.sa_flags = 0;
	elf_sig_action(&st, 10, &sa, NULL);
	elf_sigset_from_mask(&sa.sa_mask, elf_sigbit(12));
	elf_sig_action(&st, 10, &sa, NULL);

	uint64_t before = elf_sig_tls(&st)->blocked;
	ctx.rsp = sp;
	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_DELIVERED,
	   "a caught signal delivers");
	ok(elf_sig_tls(&st)->blocked == (before | elf_sigbit(10) | elf_sigbit(12)),
	   "the mask gains sa_mask and the signal itself");

	const elfsysv_sigframe_t *f = (const elfsysv_sigframe_t *)w.frame;
	ok(elf_sigset_mask(&f->uc.uc_sigmask) == before,
	   "the frame carries the mask that was in force, not the new one");
	ok(f->uc.uc_mcontext.gregs[ELF_REG_RSP] == (elfsysv_greg_t)sp,
	   "the frame carries the interrupted stack pointer");
	ok(ctx.rip == (uint64_t)(uintptr_t)handler_stub,
	   "the thread will resume in the handler");
	ok(ctx.rdi == 10, "with the signal number in rdi");
	ok(ctx.rsi == 0 && ctx.rdx == 0,
	   "and without siginfo, which was not asked for");
	ok(f->pretcode == (uint64_t)(uintptr_t)elfsysv_sig_return_tramp,
	   "the handler will return to the trampoline");

	/* SA_NODEFER leaves the signal itself unblocked. */
	elf_sig_init(&st);
	sa.sa_flags = ELFSYSV_SA_NODEFER | ELFSYSV_SA_SIGINFO;
	elf_sigemptyset(&sa.sa_mask);
	elf_sig_action(&st, 10, &sa, NULL);
	ctx.rsp = sp;
	elf_sig_deliver(&st, 10, NULL, &ctx, &w);
	ok(elf_sig_tls(&st)->blocked == 0, "SA_NODEFER does not block the signal it delivers");
	f = (const elfsysv_sigframe_t *)w.frame;
	ok(ctx.rsi == (uint64_t)(uintptr_t)&f->info,
	   "SA_SIGINFO passes the siginfo in rsi");
	ok(ctx.rdx == (uint64_t)(uintptr_t)&f->uc,
	   "and the ucontext in rdx");
	ok(f->info.si_signo == 10, "the siginfo names the signal");

	/* SA_RESETHAND puts the disposition back. */
	elf_sig_init(&st);
	sa.sa_flags = ELFSYSV_SA_RESETHAND;
	elf_sig_action(&st, 10, &sa, NULL);
	ctx.rsp = sp;
	elf_sig_deliver(&st, 10, NULL, &ctx, &w);
	ok(st.act[10].sa_handler == ELFSYSV_SIG_DFL,
	   "SA_RESETHAND restores SIG_DFL after one delivery");

	/* A blocked signal is held rather than delivered. */
	elf_sig_init(&st);
	sa.sa_flags = 0;
	elf_sig_action(&st, 10, &sa, NULL);
	elfsysv_sigset_t s;
	elf_sigset_from_mask(&s, elf_sigbit(10));
	elf_sig_procmask(&st, ELFSYSV_SIG_BLOCK, &s, NULL);
	ctx.rsp = sp;
	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_BLOCKED,
	   "a blocked signal is held");
}

/* ---- the check, over frames a handler could have written ---------------- */

static void frame_check(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa;
	elfsysv_sigctx_t ctx;
	elf_sig_placement_t w;
	static char region[64 * 1024];
	char why[ELF_SIG_WHY_MAX];
	uintptr_t sp = (uintptr_t)region + sizeof(region) - 4096;

	elf_sig_init(&st);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)handler_stub;
	elf_sig_action(&st, 10, &sa, NULL);
	memset(&ctx, 0, sizeof(ctx));
	ctx.rsp = sp;
	ctx.rip = (uint64_t)(uintptr_t)handler_stub;
	ctx.cs = 0x33;
	elf_sig_deliver(&st, 10, NULL, &ctx, &w);

	size_t len = w.top - w.frame;
	elfsysv_sigframe_t *f = (elfsysv_sigframe_t *)w.frame;

	if (!elf_sigframe_check(&st, w.frame, len, why))
		printf("#  placed frame refused: %s\n", why);
	ok(elf_sigframe_check(&st, w.frame, len, why) == 1,
	   "a frame as placed is accepted");

	uint64_t save = f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE];
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE] = save ^ 1;
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "a rewritten authenticator is refused");
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE] = save;

	uint64_t top = f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP];
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP] = top + 4096;
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "an extent stretched past the readable bytes is refused");
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP] = top;

	uint64_t pre = f->pretcode;
	f->pretcode = 0xdeadbeef;
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "a rewritten return address is refused");
	f->pretcode = pre;

	elfsysv_fpstate_t *fp = f->uc.uc_mcontext.fpregs;
	f->uc.uc_mcontext.fpregs = (elfsysv_fpstate_t *)(w.frame - 4096);
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "an fpstate pointed outside the frame is refused");
	f->uc.uc_mcontext.fpregs = fp;

	if (elf_sig_xstate_size()) {
		uint32_t m = fp->sw_reserved.magic1;
		fp->sw_reserved.magic1 = 0;
		ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
		   "an extended frame without its magic is refused");
		fp->sw_reserved.magic1 = m;

		uint64_t bv = fp->sw_reserved.xstate_bv;
		fp->sw_reserved.xstate_bv = ~(uint64_t)0;
		ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
		   "an xstate naming absent components is refused");
		fp->sw_reserved.xstate_bv = bv;
	}

	f->uc.uc_mcontext.gregs[ELF_REG_RIP] = 0x0001000000000000ll;
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "a non-canonical resume address is refused");
	f->uc.uc_mcontext.gregs[ELF_REG_RIP] = (elfsysv_greg_t)(uintptr_t)
					       handler_stub;

	f->uc.uc_mcontext.gregs[ELF_REG_RSP] = (elfsysv_greg_t)(sp + 1);
	ok(elf_sigframe_check(&st, w.frame, len, why) == 0,
	   "a misaligned resume stack pointer is refused");
	f->uc.uc_mcontext.gregs[ELF_REG_RSP] = (elfsysv_greg_t)sp;

	ok(elf_sigframe_check(&st, w.frame + 8, len, why) == 0,
	   "a frame address off by a word is refused");
	ok(elf_sigframe_check(&st, w.frame, 32, why) == 0,
	   "a readable region shorter than a frame is refused");
	ok(why[0] != '\0', "and every refusal comes with a reason");
}

/* ---- the round trip ----------------------------------------------------- */

static volatile int trial_ran;
static volatile int trial_signo;
static volatile uintptr_t trial_handler_sp;
static elfsysv_sigstate_t trial_state;
static elf_sig_placement_t trial_place;
static uintptr_t trial_interrupted_sp;

static ELF_SYSV void trial_handler(int signo)
{
	uintptr_t sp;
	__asm__ volatile("movq %%rsp, %0" : "=r"(sp));
	trial_handler_sp = sp;
	trial_signo = signo;
	trial_ran++;
}

static ELF_SYSV void trial_fire(elfsysv_sigctx_t *ctx)
{
	elf_sig_disposition_t d;

	trial_interrupted_sp = (uintptr_t)ctx->rsp;
	d = elf_sig_deliver(&trial_state, 10, NULL, ctx, &trial_place);
	if (d != ELF_SIG_DELIVERED) {
		printf("not ok - the trial delivery was refused (%d)\n",
		       (int)d);
		exit(1);
	}
	elfsysv_sig_resume(ctx);
}

extern ELF_SYSV long sig_redzone_trial(elfsysv_sigctx_t *ctx, void *stack_top,
				       ELF_SYSV void (*fire)(
					       elfsysv_sigctx_t *));

static void round_trip(int on_alt)
{
	elfsysv_sigctx_t ctx;
	elfsysv_sigaction_t sa;
	static char trial_stack[256 * 1024];
	static char alt_stack[64 * 1024];
	elfsysv_stack_t ss;
	long held;

	elf_sig_init(&trial_state);
	elfsysv_sig_current = &trial_state;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)trial_handler;
	sa.sa_flags = on_alt ? ELFSYSV_SA_ONSTACK : 0;
	elf_sig_action(&trial_state, 10, &sa, NULL);

	if (on_alt) {
		memset(&ss, 0, sizeof(ss));
		ss.ss_sp = alt_stack;
		ss.ss_size = sizeof(alt_stack);
		elf_sig_altstack(&trial_state, 0, &ss, NULL);
	}

	trial_ran = 0;
	trial_signo = 0;
	trial_handler_sp = 0;

	void *top = trial_stack + sizeof(trial_stack);
	held = sig_redzone_trial(&ctx, top, trial_fire);

	ok(trial_ran == 1, on_alt ? "the alternate-stack handler ran once"
				  : "the handler ran once");
	ok(trial_signo == 10, "with the signal it was sent");
	ok(held == 1, on_alt
			      ? "the red zone and the callee-saved registers "
				"survived an alternate-stack delivery"
			      : "the red zone and the callee-saved registers "
				"survived the delivery");
	ok(trial_interrupted_sp == (uintptr_t)top,
	   "the delivery saw the interrupted stack pointer it was given");

	if (on_alt) {
		ok(trial_handler_sp > (uintptr_t)alt_stack &&
			   trial_handler_sp <
				   (uintptr_t)alt_stack + sizeof(alt_stack),
		   "the handler ran on the alternate stack");
		ok(trial_place.on_altstack == 1,
		   "and the placement says it did");
	} else {
		ok(trial_handler_sp <= (uintptr_t)top - ELFSYSV_REDZONE,
		   "the handler ran below the reserved bytes");
		ok(trial_place.top == (uintptr_t)top - ELFSYSV_REDZONE,
		   "and nothing was written into them");
	}
}

/* ---- the per-thread split (DR-0030's deferral) --------------------------
 *
 * The provider stands in for a thread switch: two records, and a global that
 * says which thread is "calling". What is certified is the seam itself --
 * every mask and altstack access resolves through the provider, the records
 * are independent, delivery is governed by the current record, and a NULL
 * answer falls back to the embedded record so the single-thread path is the
 * old behaviour exactly.
 */

static elfsysv_sigtls_t tls_a, tls_b;
static elfsysv_sigtls_t *tls_now;

static elfsysv_sigtls_t *pick_tls(void)
{
	return tls_now;
}

static void per_thread(void)
{
	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa;
	elfsysv_sigset_t s;
	elfsysv_stack_t ss;
	elfsysv_sigctx_t ctx;
	elf_sig_placement_t w;
	static char region_b[64 * 1024];
	static char stack[64 * 1024];
	uintptr_t sp = (uintptr_t)stack + sizeof(stack) - 4096;

	elf_sig_init(&st);

	/* What a new thread starts with: the creator's mask minus the
	 * unblockable bits, and no alternate stack. */
	elf_sig_tls_init(&tls_a, elf_sigbit(10) | elf_sigbit(9));
	ok(tls_a.blocked == elf_sigbit(10),
	   "a new thread inherits the creator's mask with SIGKILL dropped");
	ok(tls_a.altstack.ss_flags == ELFSYSV_SS_DISABLE,
	   "and starts with the alternate stack disabled");

	elf_sig_tls_init(&tls_b, 0);
	elf_sig_set_tls_provider(pick_tls);

	/* The mask is per thread: blocking on B leaves A's mask alone. */
	tls_now = &tls_b;
	elf_sigset_from_mask(&s, elf_sigbit(12));
	elf_sig_procmask(&st, ELFSYSV_SIG_BLOCK, &s, NULL);
	ok(tls_b.blocked == elf_sigbit(12) && tls_a.blocked == elf_sigbit(10),
	   "blocking on one thread does not touch the other's mask");

	/* The alternate stack is per thread too. */
	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = region_b;
	ss.ss_size = sizeof(region_b);
	ok(elf_sig_altstack(&st, 0, &ss, NULL) == 0,
	   "an alternate stack installs on the calling thread");
	uintptr_t inside = (uintptr_t)region_b + 1024;
	ok(elf_sig_on_altstack(&st, inside) == 1,
	   "and that thread is on it when its stack pointer is inside");
	tls_now = &tls_a;
	ok(elf_sig_on_altstack(&st, inside) == 0,
	   "while the other thread, at the same address, is not");

	/* Delivery is governed by the calling thread's record: the same
	 * signal, under the same process state, is held on the thread that
	 * blocks it and delivered on the thread that does not. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)handler_stub;
	elf_sigemptyset(&sa.sa_mask);
	elf_sig_action(&st, 10, &sa, NULL);
	memset(&ctx, 0, sizeof(ctx));
	ctx.rsp = sp;
	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_BLOCKED,
	   "the thread that blocks the signal holds it");
	tls_now = &tls_b;
	ctx.rsp = sp;
	ok(elf_sig_deliver(&st, 10, NULL, &ctx, &w) == ELF_SIG_DELIVERED,
	   "the thread that does not, takes the delivery");
	ok((tls_b.blocked & elf_sigbit(10)) && tls_a.blocked == elf_sigbit(10),
	   "and the delivery advances only the receiving thread's mask");

	/* A NULL answer is the fallback, which is the embedded record. */
	tls_now = NULL;
	ok(elf_sig_tls(&st) == &st.tls0,
	   "a provider with no record for this thread falls back");

	elf_sig_set_tls_provider(NULL);
	ok(elf_sig_tls(&st) == &st.tls0,
	   "and no provider at all is the single-thread path unchanged");
}

int main(void)
{
	shape();
	mask_and_altstack();
	flags();
	placement();
	dispositions();
	frame_check();
	per_thread();
	round_trip(0);
	round_trip(1);

	printf("%s - %d checks, %d failed\n", failures ? "FAIL" : "ok", checks,
	       failures);
	return failures ? 1 : 0;
}
