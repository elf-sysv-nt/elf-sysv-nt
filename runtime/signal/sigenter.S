/*
 * sigenter.S -- the two ends of a delivery that cannot be written in C.
 *
 * elfsysv_sig_resume installs a register file and runs it. It is what
 * SetThreadContext does for the hijack, written out so a test can drive a
 * delivery in one thread with no host call in the loop.
 *
 * elfsysv_sig_return_tramp is the address a handler returns to. The frame's
 * pretcode word holds it, so a handler's own `ret` lands here with rsp one
 * word above the frame, which is where the frame address comes from.
 *
 * elfsysv_sig_restore ends a return. Every general register comes from the
 * saved file and the last three -- rip, rflags and rsp -- come from an iret
 * frame built on the stack the runtime owns. The obvious alternative is
 * setcontext's: load rsp from the file, push the return address, ret. That
 * push lands at the destination rsp minus eight, which is inside the red zone
 * this package exists to keep, so it is not available here. DR-0030.
 */

#ifdef __ELF__
# define PROC(n)	.globl n; .type n,@function; n:
# define ENDPROC(n)	.size n, .-n
# define PROLOGUE_END
#else
# define PROC(n)	.globl n; n: ; .seh_proc n
# define ENDPROC(n)	.seh_endproc
# define PROLOGUE_END	.seh_endprologue
#endif

	.text

/* void elfsysv_sig_resume(const elfsysv_sigctx_t *ctx) -- does not return.
 * Offsets are asserted against the struct in sigframe.c. */
PROC(elfsysv_sig_resume)
	PROLOGUE_END
	movq	%rdi, %r11
	movq	%ss, %rax
	pushq	%rax			/* SS                                */
	pushq	56(%r11)		/* RSP                               */
	pushq	136(%r11)		/* RFLAGS                            */
	movq	%cs, %rax
	pushq	%rax			/* CS                                */
	pushq	128(%r11)		/* RIP                               */
	movq	0(%r11), %rax
	movq	8(%r11), %rbx
	movq	16(%r11), %rcx
	movq	24(%r11), %rdx
	movq	32(%r11), %rsi
	movq	40(%r11), %rdi
	movq	48(%r11), %rbp
	movq	64(%r11), %r8
	movq	72(%r11), %r9
	movq	80(%r11), %r10
	movq	96(%r11), %r12
	movq	104(%r11), %r13
	movq	112(%r11), %r14
	movq	120(%r11), %r15
	movq	88(%r11), %r11
	iretq
ENDPROC(elfsysv_sig_resume)

/* void elfsysv_sig_restore(const elfsysv_greg_t *gregs) -- does not return.
 * Indices are Linux's REG_* order; the byte offsets below are index * 8. */
PROC(elfsysv_sig_restore)
	PROLOGUE_END
	movq	%rdi, %r11
	movq	%ss, %rax
	pushq	%rax			/* SS                                */
	pushq	120(%r11)		/* RSP   (REG_RSP  = 15)             */
	pushq	136(%r11)		/* EFL   (REG_EFL  = 17)             */
	movq	%cs, %rax
	pushq	%rax			/* CS                                */
	pushq	128(%r11)		/* RIP   (REG_RIP  = 16)             */
	movq	0(%r11), %r8		/* REG_R8  = 0                       */
	movq	8(%r11), %r9
	movq	16(%r11), %r10
	movq	32(%r11), %r12
	movq	40(%r11), %r13
	movq	48(%r11), %r14
	movq	56(%r11), %r15
	movq	64(%r11), %rdi		/* REG_RDI = 8                       */
	movq	72(%r11), %rsi
	movq	80(%r11), %rbp
	movq	88(%r11), %rbx
	movq	96(%r11), %rdx
	movq	104(%r11), %rax
	movq	112(%r11), %rcx
	movq	24(%r11), %r11		/* REG_R11 = 3, last                 */
	iretq
ENDPROC(elfsysv_sig_restore)

/* void elfsysv_sig_enter(void) -- entered by the hijack with r11 pointing at
 * the pending delivery, running on the interrupted thread's own stack.
 *
 * The frame cannot be built by the thread that sends the signal. A Windows
 * stack is reserved and grows by faulting on a guard page, and that fault is
 * only a stack extension for the thread the stack belongs to: a write from
 * another thread reaches the same page and takes an unhandled guard-page
 * exception. So the hijack only points the target here, and the target builds
 * its own frame. This is the trampoline the work package is named for, and its
 * shape is Cygwin's: the delivering thread rewrites a register file, and the
 * receiving thread does the work.
 *
 * The gap below is stepped one page at a time. Moving the stack pointer past a
 * guard page without touching it leaves the page below unmapped, so a large
 * subtract followed by a push is how a stack fails to grow. */
PROC(elfsysv_sig_enter)
	PROLOGUE_END
	subq	$4096, %rsp
	movq	$0, (%rsp)
	subq	$4096, %rsp
	movq	$0, (%rsp)
	andq	$-16, %rsp
	movq	%r11, %rdi
	call	elfsysv_sig_enter_c
	ud2
ENDPROC(elfsysv_sig_enter)

/* The handler's return address. rsp is one word above the frame here, because
 * the handler's own ret popped the pretcode word this address was written
 * into. Step rsp below the frame before the call so the pushed return address
 * does not land in bytes elfsysv_sigreturn is about to validate. */
PROC(elfsysv_sig_return_tramp)
	PROLOGUE_END
	leaq	-8(%rsp), %rdi
	subq	$16, %rsp
	call	elfsysv_sigreturn
	ud2
ENDPROC(elfsysv_sig_return_tramp)

#ifdef __ELF__
	.section .note.GNU-stack,"",@progbits
#endif
