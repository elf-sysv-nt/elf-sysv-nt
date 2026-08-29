/*
 * The ELF side of spike 2.
 *
 * Assembled by Cygwin's as into a COFF object and then flattened with
 * objcopy -O binary, so what make-elf.py embeds is raw machine code with no
 * relocations in it. That constrains how this is written: nothing here may
 * name an address, because there is no linker to fill one in. Every address
 * it touches arrives either on the stack the stub built or through %rip.
 *
 * It is entered by a jump, not a call, with %rsp on argc exactly as the psABI
 * says a process entry point is entered. It leaves by restoring the host
 * stack pointer the trampoline parked for it and returning, because there is
 * no exit to call: the down-call surface is WP-41 and this spike predates it.
 *
 * What it proves it can do, it proves by writing into the handshake block at
 * the head of the writable segment. Reading the read-only segment, reading
 * .bss past the end of the file image, walking argv and envp to auxv, and
 * running at all -- each shows up there as a word the stub checks.
 *
 * Before it leaves it poisons every register the Microsoft ABI calls
 * callee-saved and the System V ABI does not. Nothing on the ELF side is
 * obliged to preserve those, so the trampoline is, and this is what makes
 * that claim testable rather than assumed.
 */

	.text

	/* Spike-local auxv keys. Nothing standard occupies 0x7000. */
	.set	AT_NULL,		0
	.set	AT_PAGESZ,		6
	.set	AT_SPIKE_HANDSHAKE,	0x7000
	.set	AT_SPIKE_RODATA,	0x7001
	.set	AT_SPIKE_BSS,		0x7002

	/* Handshake block, at the head of the writable segment. */
	.set	HS_IN_MAGIC,	0x00
	.set	HS_HOST_RSP,	0x08
	.set	HS_OUT_MAGIC,	0x10
	.set	HS_OUT_ARGC,	0x18
	.set	HS_OUT_RODATA,	0x20
	.set	HS_OUT_BSS,	0x28
	.set	HS_OUT_RIP,	0x30
	.set	HS_OUT_PAGESZ,	0x38
	.set	HS_OUT_ARGV0,	0x40

	.globl	payload_start
payload_start:
	movq	(%rsp), %rbx		/* argc */
	leaq	8(%rsp), %rcx		/* argv */
	leaq	8(%rcx,%rbx,8), %rcx	/* past argv and its terminator: envp */
1:	movq	(%rcx), %rax
	addq	$8, %rcx
	testq	%rax, %rax
	jnz	1b			/* %rcx now on the first auxv key */

	xorq	%r10, %r10		/* handshake */
	xorq	%r11, %r11		/* read-only word */
	xorq	%r8, %r8		/* .bss probe */
	xorq	%r9, %r9		/* page size */
2:	movq	(%rcx), %rax
	movq	8(%rcx), %rdx
	addq	$16, %rcx
	testq	%rax, %rax
	jz	3f
	cmpq	$AT_SPIKE_HANDSHAKE, %rax
	cmoveq	%rdx, %r10
	cmpq	$AT_SPIKE_RODATA, %rax
	cmoveq	%rdx, %r11
	cmpq	$AT_SPIKE_BSS, %rax
	cmoveq	%rdx, %r8
	cmpq	$AT_PAGESZ, %rax
	cmoveq	%rdx, %r9
	jmp	2b
3:
	/* No handshake block means no way to report anything, and no way to
	   leave either, since the host stack pointer is parked in it. Spin
	   rather than fault, so the stub's timeout says which of the two went
	   wrong instead of a crash saying neither. */
	testq	%r10, %r10
	jz	3b

	movq	HS_IN_MAGIC(%r10), %rax
	movabsq	$0x9E3779B97F4A7C15, %rdx
	xorq	%rdx, %rax
	movq	%rax, HS_OUT_MAGIC(%r10)

	movq	%rbx, HS_OUT_ARGC(%r10)
	movq	(%r11), %rax		/* the read-only segment */
	movq	%rax, HS_OUT_RODATA(%r10)
	movq	(%r8), %rax		/* past p_filesz: .bss, and it wants zeroing */
	movq	%rax, HS_OUT_BSS(%r10)
	movq	%r9, HS_OUT_PAGESZ(%r10)

	movq	8(%rsp), %rax		/* argv[0] */
	movq	(%rax), %rax
	movq	%rax, HS_OUT_ARGV0(%r10)

.Lhere:
	leaq	.Lhere(%rip), %rax
	movq	%rax, HS_OUT_RIP(%r10)

	/* Everything the Microsoft ABI expects back, deliberately destroyed. */
	movabsq	$0xDEADBEEFDEADBEEF, %rax
	movq	%rax, %rbx
	movq	%rax, %rbp
	movq	%rax, %rdi
	movq	%rax, %rsi
	movq	%rax, %r12
	movq	%rax, %r13
	movq	%rax, %r14
	movq	%rax, %r15
	pcmpeqd	%xmm6, %xmm6
	pcmpeqd	%xmm7, %xmm7
	pcmpeqd	%xmm8, %xmm8
	pcmpeqd	%xmm9, %xmm9
	pcmpeqd	%xmm10, %xmm10
	pcmpeqd	%xmm11, %xmm11
	pcmpeqd	%xmm12, %xmm12
	pcmpeqd	%xmm13, %xmm13
	pcmpeqd	%xmm14, %xmm14
	pcmpeqd	%xmm15, %xmm15

	movq	HS_HOST_RSP(%r10), %rsp
	ret
	.globl	payload_end
payload_end:
