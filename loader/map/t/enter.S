/* WP-32 test scaffolding: enter a mapped ELF image and come back.
 *
 * This is not the loader's entry path -- building the psABI stack and reaching
 * e_entry is WP-40 and WP-41. It exists only so the mapping test can prove the
 * image it placed actually executes. The harness is Cygwin code, which on
 * x86-64 uses the Microsoft ABI (first two integer arguments in %rcx and
 * %rdx and %rsi/%rdi callee-saved); the specimen is compiled by the cross
 * toolchain to the System V ABI (first argument in %rdi). So this trampoline
 * bridges the two: it saves the Microsoft-ABI callee-saved integer registers,
 * moves the handshake pointer into %rdi, aligns the stack, calls the image's
 * entry, and restores. The specimen returns with a plain ret.
 *
 *   void elf_enter(void *entry, void *handshake);
 *     entry     -> %rcx      handshake -> %rdx
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
