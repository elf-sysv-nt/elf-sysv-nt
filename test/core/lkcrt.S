/*
 * lkcrt.S -- _start for the core's freestanding test programs.
 *
 * Finds the gate in the auxiliary vector (AT_SYSINFO, 32), stores it where
 * lksys.h's shim reads it, calls lk_main(argc, argv, envp) on the psABI's
 * entry stack, and leaves through exit_group with what it returned.  Under
 * the oracle build there is no gate in the auxv and none is needed; the walk
 * simply finds AT_NULL and moves on.
 *
 * Build: x86_64-elfsysvnt-linux-gnu-gcc -nostdlib -static -no-pie.
 */
	.text
	.globl	_start
_start:
	/* The substrate enters a fresh thread as if through a call; a real
	 * kernel does not.  LK_ORACLE builds skip the step. */
#ifndef LK_ORACLE
	leaq	8(%rsp), %rsp
#endif
	movq	(%rsp), %rdi		/* argc */
	leaq	8(%rsp), %rsi		/* argv */
	leaq	16(%rsp,%rdi,8), %rdx	/* envp: past argc, argv[], NULL */
	movq	%rdx, %rcx
1:	movq	(%rcx), %rax
	addq	$8, %rcx
	testq	%rax, %rax
	jnz	1b			/* rcx now at the auxv */
2:	movq	(%rcx), %rax
	testq	%rax, %rax
	je	3f
	cmpq	$32, %rax
	jne	4f
	movq	8(%rcx), %rax
	movq	%rax, lk_gate(%rip)
	jmp	3f
4:	addq	$16, %rcx
	jmp	2b
3:
	andq	$-16, %rsp		/* the ABI's alignment at a call */
	call	lk_main
	movl	%eax, %edi
	movq	$231, %rax		/* exit_group */
#ifdef LK_ORACLE
	syscall
#else
	movq	lk_gate(%rip), %r15
	call	*%r15
#endif
	ud2

	.data
	.globl	lk_gate
lk_gate:
	.quad	0

	.section .note.GNU-stack,"",@progbits
