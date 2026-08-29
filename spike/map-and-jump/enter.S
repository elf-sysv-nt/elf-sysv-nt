/*
 * The Windows side of the boundary: the trampoline that hands control to the
 * ELF image, the harness that measures whether the trampoline kept its
 * promise, and two probes that ask a page whether it means what VirtualQuery
 * says it means.
 *
 * Everything here is Microsoft ABI on its outside. The trampoline is the only
 * thing in the spike that is System V on its inside, and it is deliberately
 * thin: this is not spike 3, which asks the harder version of the question
 * with a signal running through it. All this one has to survive is a call and
 * a return.
 *
 * Written in assembly rather than in C with inline asm because a stack switch
 * is not expressible in C, and because a compiler that believes it owns the
 * frame will spill into a stack that is about to stop existing.
 */

	.text

/*
 * void elf_enter(void *entry, void *elf_rsp, void *handshake)
 *
 * Saves everything Windows expects a callee to preserve, parks its own stack
 * pointer in the handshake block, switches to the stack the stub built, and
 * jumps. The image is entered by jmp rather than call, because the psABI puts
 * argc at (%rsp) on entry and a call would put a return address there.
 *
 * The return path is the image restoring that parked pointer and executing a
 * ret, which lands on elf_enter_resume. An image that never does so never
 * comes back, and that is the honest shape: a real exec does not return
 * either.
 *
 * Built with -DSPIKE_NO_SAVE the saving is left out and everything else
 * stays. That build is the negative control: it is what abi_probe reports
 * when the trampoline does nothing, and t/run-tests.sh watches the check fail
 * against it before believing it passing against this one.
 */
	.globl	elf_enter
elf_enter:
#ifndef SPIKE_NO_SAVE
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
#endif
	leaq	elf_enter_resume(%rip), %rax
	pushq	%rax
	movq	%rsp, 8(%r8)		/* handshake->host_rsp */
	movq	%rdx, %rsp
	jmp	*%rcx

	.globl	elf_enter_resume
elf_enter_resume:
#ifndef SPIKE_NO_SAVE
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
#endif
	ret

/*
 * unsigned abi_probe(void *entry, void *elf_rsp, void *handshake,
 *                    unsigned *xmm_mask)
 *
 * Loads a sentinel into every register Windows calls callee-saved, goes
 * through elf_enter, and reports which ones came back changed: the return
 * value is a bitmask over rbx, rbp, rdi, rsi, r12, r13, r14, r15 in that
 * order, and *xmm_mask is the same over xmm6 through xmm15.
 *
 * Zero from both is the only interesting answer. The image poisons all
 * eighteen on its way out, so a trampoline that saved nothing would report
 * 0xff and 0x3ff rather than crashing somewhere later and blaming something
 * else.
 */
	.globl	abi_probe
abi_probe:
	pushq	%rbx
	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$184, %rsp		/* 160 xmm, 8 for the mask pointer, 16 spare */
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
	movq	%r9, 160(%rsp)

	leaq	xmm_sentinel(%rip), %rax
	movaps	0(%rax), %xmm6
	movaps	16(%rax), %xmm7
	movaps	32(%rax), %xmm8
	movaps	48(%rax), %xmm9
	movaps	64(%rax), %xmm10
	movaps	80(%rax), %xmm11
	movaps	96(%rax), %xmm12
	movaps	112(%rax), %xmm13
	movaps	128(%rax), %xmm14
	movaps	144(%rax), %xmm15

	movq	$0x5A5A0001, %rbx
	movq	$0x5A5A0002, %rbp
	movq	$0x5A5A0003, %rdi
	movq	$0x5A5A0004, %rsi
	movq	$0x5A5A0005, %r12
	movq	$0x5A5A0006, %r13
	movq	$0x5A5A0007, %r14
	movq	$0x5A5A0008, %r15

	subq	$32, %rsp		/* the shadow space the ABI asks for */
	call	elf_enter
	addq	$32, %rsp

	xorl	%eax, %eax
	cmpq	$0x5A5A0001, %rbx
	je	.Lg1
	orl	$0x01, %eax
.Lg1:	cmpq	$0x5A5A0002, %rbp
	je	.Lg2
	orl	$0x02, %eax
.Lg2:	cmpq	$0x5A5A0003, %rdi
	je	.Lg3
	orl	$0x04, %eax
.Lg3:	cmpq	$0x5A5A0004, %rsi
	je	.Lg4
	orl	$0x08, %eax
.Lg4:	cmpq	$0x5A5A0005, %r12
	je	.Lg5
	orl	$0x10, %eax
.Lg5:	cmpq	$0x5A5A0006, %r13
	je	.Lg6
	orl	$0x20, %eax
.Lg6:	cmpq	$0x5A5A0007, %r14
	je	.Lg7
	orl	$0x40, %eax
.Lg7:	cmpq	$0x5A5A0008, %r15
	je	.Lg8
	orl	$0x80, %eax
.Lg8:
	movl	%eax, %r11d
	xorl	%r10d, %r10d
	leaq	xmm_sentinel(%rip), %rcx

	movaps	0(%rcx), %xmm0
	pcmpeqb	%xmm6, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx1
	orl	$0x001, %r10d
.Lx1:	movaps	16(%rcx), %xmm0
	pcmpeqb	%xmm7, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx2
	orl	$0x002, %r10d
.Lx2:	movaps	32(%rcx), %xmm0
	pcmpeqb	%xmm8, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx3
	orl	$0x004, %r10d
.Lx3:	movaps	48(%rcx), %xmm0
	pcmpeqb	%xmm9, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx4
	orl	$0x008, %r10d
.Lx4:	movaps	64(%rcx), %xmm0
	pcmpeqb	%xmm10, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx5
	orl	$0x010, %r10d
.Lx5:	movaps	80(%rcx), %xmm0
	pcmpeqb	%xmm11, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx6
	orl	$0x020, %r10d
.Lx6:	movaps	96(%rcx), %xmm0
	pcmpeqb	%xmm12, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx7
	orl	$0x040, %r10d
.Lx7:	movaps	112(%rcx), %xmm0
	pcmpeqb	%xmm13, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx8
	orl	$0x080, %r10d
.Lx8:	movaps	128(%rcx), %xmm0
	pcmpeqb	%xmm14, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx9
	orl	$0x100, %r10d
.Lx9:	movaps	144(%rcx), %xmm0
	pcmpeqb	%xmm15, %xmm0
	pmovmskb %xmm0, %edx
	cmpl	$0xffff, %edx
	je	.Lx10
	orl	$0x200, %r10d
.Lx10:
	movq	160(%rsp), %rcx
	movl	%r10d, (%rcx)

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
	movl	%r11d, %eax
	addq	$184, %rsp
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
 * int probe_store(void *addr)   int probe_exec(void *addr)
 *
 * VirtualQuery reports what Windows has written down about a page. These ask
 * the page. The distinction is not pedantry: the whole reason the segment
 * protections are set is that a wrong one is silent until something writes
 * through it, and a spike that only reads back its own request has measured
 * its own request.
 *
 * Neither returns normally on a fault. The stub's vectored handler moves
 * %rip to the fault label, which is why the ranges below are exported: the
 * handler decides whether an access violation is one of these probes by
 * asking whether %rip is inside one, and anything else it declines so that a
 * genuine crash still crashes.
 */
	.globl	probe_store
	.globl	probe_store_end
	.globl	probe_store_fault
probe_store:
	movq	$0x5A5A5A5A, (%rcx)
	xorl	%eax, %eax
	ret
probe_store_end:
probe_store_fault:
	movl	$1, %eax
	ret

/*
 * The execute probe faults inside the target rather than inside itself, so
 * %rip is no help and the handler recognizes it by the armed flag instead.
 * That leaves the stack unbalanced at the moment of the fault -- a call has
 * pushed a return address that will never be popped -- so the entry value is
 * parked and the fault path restores it.
 */
	.globl	probe_exec
	.globl	probe_exec_end
	.globl	probe_exec_fault
probe_exec:
	movq	%rsp, probe_exec_rsp(%rip)
	subq	$40, %rsp
	call	*%rcx
	xorl	%eax, %eax
	addq	$40, %rsp
	ret
probe_exec_end:
probe_exec_fault:
	movq	probe_exec_rsp(%rip), %rsp
	movl	$1, %eax
	ret

	.data
	.balign	16
xmm_sentinel:
	.quad	0x1111111100000006, 0x6000000011111111
	.quad	0x2222222200000007, 0x7000000022222222
	.quad	0x3333333300000008, 0x8000000033333333
	.quad	0x4444444400000009, 0x9000000044444444
	.quad	0x555555550000000A, 0xA000000055555555
	.quad	0x666666660000000B, 0xB000000066666666
	.quad	0x777777770000000C, 0xC000000077777777
	.quad	0x888888880000000D, 0xD000000088888888
	.quad	0x999999990000000E, 0xE000000099999999
	.quad	0xAAAAAAAA0000000F, 0xF0000000AAAAAAAA

	.balign	8
probe_exec_rsp:
	.quad	0
