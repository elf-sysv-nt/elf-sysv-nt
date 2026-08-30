/*
 * trial.S -- a delivery driven end to end with no host thread in the loop.
 *
 * The problem a red-zone test has is that the test is written on the stack it
 * wants to watch: painting the bytes below rsp and then calling anything at
 * all overwrites them with a return address. So this runs the interrupted side
 * on a stack of its own. The caller supplies a region; the trial builds a
 * register file that resumes at label 1 on that region, paints the sixteen
 * words below its stack pointer, and hands the file to a callback that
 * delivers a signal into it and resumes it.
 *
 * Everything after that is the real path: the frame is placed, the handler
 * runs, the handler returns to the trampoline, the trampoline calls
 * elfsysv_sigreturn, and the restore iretqs back to label 1. What arrives
 * there is what a returning signal is supposed to leave: the same stack
 * pointer, the same callee-saved registers, and sixteen unbroken words below.
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

#ifdef __ELF__
# define SEH_PUSH(r)
#else
# define SEH_PUSH(r)	.seh_pushreg r
#endif

#define PAINT	0x5245445a4f4e4531	/* "1ENOZDER" little-endian          */
#define MARKER	0x1234567890abcdef

	.text

/* long sig_redzone_trial(elfsysv_sigctx_t *ctx, void *stack_top,
 *			  void (*fire)(elfsysv_sigctx_t *));
 *
 * Returns 1 when the red zone below stack_top survived the delivery and the
 * marker in r12 came back, 0 otherwise. */
PROC(sig_redzone_trial)
	pushq	%rbx
	SEH_PUSH(%rbx)
	pushq	%rbp
	SEH_PUSH(%rbp)
	pushq	%r12
	SEH_PUSH(%r12)
	pushq	%r13
	SEH_PUSH(%r13)
	pushq	%r14
	SEH_PUSH(%r14)
	pushq	%r15
	SEH_PUSH(%r15)
	PROLOGUE_END

	movq	%rdi, %rbx		/* ctx                               */
	movq	%rsi, %r10		/* stack_top                         */
	movq	%rdx, %r13		/* fire                              */
	movq	%rsp, %r14		/* the stack to come back to         */

	/* Zero the register file, then fill the parts a resume needs. */
	xorl	%eax, %eax
	movl	$20, %ecx		/* the whole elfsysv_sigctx_t        */
	movq	%rbx, %r11
1:	movq	%rax, (%r11)
	addq	$8, %r11
	decl	%ecx
	jnz	1b

	leaq	9f(%rip), %rax
	movq	%rax, 128(%rbx)		/* rip                               */
	movq	%r10, 56(%rbx)		/* rsp                               */
	pushfq
	popq	%rax
	movq	%rax, 136(%rbx)		/* rflags                            */
	movq	%cs, %rax
	movw	%ax, 144(%rbx)
	movq	%ss, %rax
	movw	%ax, 146(%rbx)

	/* Callee-saved registers travel through the delivery, so the ones this
	 * function needs on the far side are put in the file, and r12 carries
	 * a marker that proves the restore rather than the compiler. */
	movq	%rbx, 8(%rbx)		/* rbx                               */
	movq	%rbp, 48(%rbx)		/* rbp                               */
	movabsq	$MARKER, %rax
	movq	%rax, 96(%rbx)		/* r12                               */
	movq	%r13, 104(%rbx)		/* r13                               */
	movq	%r14, 112(%rbx)		/* r14                               */
	movq	%r15, 120(%rbx)		/* r15                               */

	/* Paint the sixteen words the psABI reserves below stack_top. */
	movabsq	$PAINT, %rax
	movq	%r10, %r11
	movl	$16, %ecx
2:	subq	$8, %r11
	movq	%rax, (%r11)
	decl	%ecx
	jnz	2b

	movq	%rbx, %rdi
	call	*%r13
	ud2				/* fire resumes; it does not return  */

	/* --- the far side, running on the trial stack --------------------- */
9:	movq	%rsp, %r8		/* == stack_top if the return held   */
	movl	$1, %r9d

	movabsq	$PAINT, %rax
	movq	%r8, %r11
	movl	$16, %ecx
3:	subq	$8, %r11
	cmpq	%rax, (%r11)
	je	4f
	xorl	%r9d, %r9d
4:	decl	%ecx
	jnz	3b

	movabsq	$MARKER, %rax
	cmpq	%rax, %r12
	je	5f
	xorl	%r9d, %r9d
5:	movq	%r9, %rax

	movq	%r14, %rsp		/* back to the caller's own stack    */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	ret
ENDPROC(sig_redzone_trial)

#ifdef __ELF__
	.section .note.GNU-stack,"",@progbits
#endif
