/*
 * WP-41's end-to-end specimen: a static ELF that reads the process image it
 * was entered on and reports what it found by leaving.
 *
 * Freestanding and hand-written, because there is no libc for this target yet
 * and because the point is to check what the entry contract delivered rather
 * than what a startup file makes of it. It reads argc, argv, envp and the auxv
 * off the stack exactly where the kernel puts them, touches its own read-only
 * and zero-filled data to prove the segments were mapped and protected, and
 * accumulates one bit per check.
 *
 * It leaves through the address the host handed it in %rdx, which the psABI
 * reserves for the termination handler the program is to register. Here that
 * handler ends the process, and it is called with the status in %rdi, which is
 * the System V argument register. That is the whole exit protocol: no syscall
 * is made, because the syscall surface is a later package, and a specimen that
 * needed one would be testing that package instead of this one.
 *
 * The bits, low to high:
 *   0x01  argc is 3, which is what the driver passes
 *   0x02  argv[0] is a pointer to a non-empty string
 *   0x04  envp terminates within a sane number of entries
 *   0x08  the auxv carries AT_PAGESZ and it is 4096
 *   0x10  %rsp was 16-byte aligned at entry
 *   0x20  the read-only segment holds its sentinel
 *   0x40  the byte past p_filesz in the writable segment is zero
 * so 0x7f is the passing status and any other value names what went wrong.
 */

	.section .rodata
	.align 8
sentinel:
	.quad 0xC0FFEE0FBADC0DE5

	.bss
	.align 8
bssword:
	.quad 0

	.text
	.globl _start
_start:
	movq	%rsp, %rbp		/* the entry stack, kept intact */
	movq	%rdx, %r15		/* the host's termination handler */
	xorl	%r14d, %r14d		/* the status accumulator */

	/* 0x10: the psABI's alignment requirement at _start. */
	testq	$15, %rbp
	jnz	1f
	orq	$0x10, %r14
1:
	/* 0x01: argc, at (%rsp). */
	movq	(%rbp), %rax
	cmpq	$3, %rax
	jne	2f
	orq	$0x01, %r14
2:
	/* 0x02: argv[0], one word above argc. */
	movq	8(%rbp), %rcx
	testq	%rcx, %rcx
	jz	3f
	cmpb	$0, (%rcx)
	je	3f
	orq	$0x02, %r14
3:
	/* envp begins two words past the last argv entry: the argv
	   terminator, then the first environment pointer. */
	movq	(%rbp), %rax
	leaq	16(%rbp,%rax,8), %rdi
	movl	$4096, %ecx
4:
	movq	(%rdi), %rdx
	testq	%rdx, %rdx
	jz	5f
	addq	$8, %rdi
	loop	4b
	jmp	6f
5:
	orq	$0x04, %r14		/* 0x04: envp is terminated */
6:
	addq	$8, %rdi		/* past the envp terminator: the auxv */
	movl	$256, %ecx
7:
	movq	(%rdi), %rax
	testq	%rax, %rax
	jz	9f			/* AT_NULL: the auxv ended */
	cmpq	$6, %rax		/* AT_PAGESZ */
	jne	8f
	cmpq	$4096, 8(%rdi)
	jne	9f
	orq	$0x08, %r14
	jmp	9f
8:
	addq	$16, %rdi
	loop	7b
9:
	/* 0x20: the read-only segment was mapped with its contents. */
	movabsq	$0xC0FFEE0FBADC0DE5, %rax
	cmpq	%rax, sentinel(%rip)
	jne	10f
	orq	$0x20, %r14
10:
	/* 0x40: what the object left past p_filesz is zero. */
	cmpq	$0, bssword(%rip)
	jne	11f
	orq	$0x40, %r14
11:
	movq	%r14, %rdi
	jmp	*%r15

	.section .note.GNU-stack,"",@progbits
