/*
 * The Windows side of WP-40's entry: the trampoline that jumps to the image on
 * the psABI stack the builder laid, and a probe that proves the crossing left
 * the Microsoft ABI's callee-saved registers intact.
 *
 * This is the entry contract made concrete. proc_build_stack reports the %rsp
 * to enter on and the %rdx to carry; proc_enter sets exactly those and reaches
 * e_entry by jmp, because argc belongs at (%rsp) and a call would put a return
 * address there. The image comes back by restoring the pointer parked in the
 * handshake and executing ret, the same shape spike 2 used: a real exec does
 * not return either, so the return path is a courtesy to the test rather than
 * the program's own.
 *
 * Everything here is Microsoft ABI on its outside and System V on its inside.
 * Written in assembly because a stack switch is not expressible in C and a
 * compiler that believes it owns the frame would spill into a stack about to
 * stop existing.
 */

	.text

/*
 * void proc_enter(void *entry, void *sp, uint64_t rdx, void *handshake)
 *   entry -> %rcx   sp -> %rdx   rdx -> %r8   handshake -> %r9
 *
 * Saves the Microsoft-ABI callee-saved registers, parks its resume address on
 * its own stack and records that stack pointer in handshake->host_rsp (offset
 * 8), sets %rdx to the atexit value the ELF entry expects, switches to the
 * built stack, and jumps to the entry.
 */
	.globl	proc_enter
proc_enter:
	pushq	%rbx
	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$168, %rsp		/* 160 for xmm6-15, 8 to realign */
	movaps	%xmm6, 0(%rsp)
	movaps	%xmm7, 16(%rsp)
	movaps	%xmm8, 32(%rsp)
	movaps	%xmm9, 48(%rsp)
	movaps	%xmm10, 64(%rsp)
	movaps	%xmm11, 80(%rsp)
	movaps	%xmm12, 96(%rsp)
	movaps	%xmm13, 112(%rsp)
	movaps	%xmm14, 128(%rsp)
	movaps	%xmm15, 144(%rsp)

	leaq	proc_enter_resume(%rip), %rax
	pushq	%rax			/* the return the image lands on */
	movq	%rsp, 8(%r9)		/* handshake->host_rsp */
	movq	%rdx, %rax		/* the built %rsp, before %rdx is reset */
	movq	%r8, %rdx		/* the atexit handler for the ELF */
	movq	%rax, %rsp		/* switch to the built stack */
	jmp	*%rcx			/* enter e_entry with argc at (%rsp) */

	.globl	proc_enter_resume
proc_enter_resume:
	movaps	0(%rsp), %xmm6
	movaps	16(%rsp), %xmm7
	movaps	32(%rsp), %xmm8
	movaps	48(%rsp), %xmm9
	movaps	64(%rsp), %xmm10
	movaps	80(%rsp), %xmm11
	movaps	96(%rsp), %xmm12
	movaps	112(%rsp), %xmm13
	movaps	128(%rsp), %xmm14
	movaps	144(%rsp), %xmm15
	addq	$168, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%rbx
	ret

/*
 * unsigned proc_abi_probe(void *entry, void *sp, uint64_t rdx, void *handshake)
 *
 * Loads a sentinel into every integer register the Microsoft ABI calls
 * callee-saved, crosses through proc_enter, and returns a bitmask of the ones
 * that came back changed: rbx, rbp, rdi, rsi, r12, r13, r14, r15 in that
 * order. Zero is the only passing answer. It proves the trampoline's save and
 * restore actually happened rather than being trusted.
 */
	.globl	proc_abi_probe
proc_abi_probe:
	pushq	%rbx
	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$40, %rsp		/* shadow space + realign */

	movq	$0x5A5A0001, %rbx
	movq	$0x5A5A0002, %rbp
	movq	$0x5A5A0003, %rdi
	movq	$0x5A5A0004, %rsi
	movq	$0x5A5A0005, %r12
	movq	$0x5A5A0006, %r13
	movq	$0x5A5A0007, %r14
	movq	$0x5A5A0008, %r15

	call	proc_enter		/* args are already in rcx/rdx/r8/r9 */

	xorl	%eax, %eax
	cmpq	$0x5A5A0001, %rbx
	je	.Lp1
	orl	$0x01, %eax
.Lp1:	cmpq	$0x5A5A0002, %rbp
	je	.Lp2
	orl	$0x02, %eax
.Lp2:	cmpq	$0x5A5A0003, %rdi
	je	.Lp3
	orl	$0x04, %eax
.Lp3:	cmpq	$0x5A5A0004, %rsi
	je	.Lp4
	orl	$0x08, %eax
.Lp4:	cmpq	$0x5A5A0005, %r12
	je	.Lp5
	orl	$0x10, %eax
.Lp5:	cmpq	$0x5A5A0006, %r13
	je	.Lp6
	orl	$0x20, %eax
.Lp6:	cmpq	$0x5A5A0007, %r14
	je	.Lp7
	orl	$0x40, %eax
.Lp7:	cmpq	$0x5A5A0008, %r15
	je	.Lp8
	orl	$0x80, %eax
.Lp8:
	addq	$40, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%rbx
	ret
