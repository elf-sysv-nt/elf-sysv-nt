/* WP-34 test scaffolding: enter a mapped ELF image and come back.
 *
 * The same bridge WP-32's test used. The harness is Cygwin code on the
 * Microsoft ABI; the specimen is System V. This saves the Microsoft-ABI
 * callee-saved integer registers, moves the handshake pointer into %rdi,
 * aligns the stack, calls the image's entry, and restores.
 *
 *   void elf_enter(void *entry, void *handshake);
 *     entry -> %rcx      handshake -> %rdx
 */
	.text
	.globl	elf_enter
	.p2align 4
elf_enter:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rdi
	pushq	%rsi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rdx, %rdi		/* System V arg0 = handshake */
	andq	$-16, %rsp		/* 16-byte align for the callee */
	call	*%rcx			/* enter the mapped image */
	leaq	-56(%rbp), %rsp		/* back to the saved-register block */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rsi
	popq	%rdi
	popq	%rbx
	popq	%rbp
	ret
