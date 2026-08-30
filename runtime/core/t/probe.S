/*
 * probe.S -- the register-integrity callers WP-22's test cannot let a compiler
 * write, and two leaky targets that make the check prove itself.
 *
 * The method is spike 3's, narrowed to what WP-22 certifies. A caller that
 * checks whether an entry point preserved the callee-saved set its convention
 * promises must poison those registers itself and read them back itself: a
 * caller the compiler wrote would emit the very convention thunk being
 * measured, and a check expressed in terms of the thing it checks measures
 * nothing. So each probe loads a sentinel into every register the callee is
 * obliged to preserve, makes the call, and reports one bit per register that
 * came back changed. Zero is the only good answer.
 *
 * Both probes are entered Microsoft x64 from the C test. ms_cross_probe stands
 * in for Windows calling an ms_abi entry point: it poisons the full Microsoft
 * callee-saved set -- rbx, rbp, rsi, rdi, r12 through r15, and xmm6 through
 * xmm15. sysv_cross_probe stands in for the runtime reaching a System V body:
 * it poisons the System V callee-saved set -- rbx, rbp, r12 through r15 -- and,
 * being itself an ms_abi callee, saves and restores the wider Microsoft set
 * around the call so its own caller is not disturbed.
 *
 * Compiled -mno-red-zone with the unit; nothing here relies on a red zone.
 */

	.data
	.align	16
xmm_seed:
	.quad	0x584d4d0000000006, 0
	.quad	0x584d4d0000000007, 0
	.quad	0x584d4d0000000008, 0
	.quad	0x584d4d0000000009, 0
	.quad	0x584d4d000000000a, 0
	.quad	0x584d4d000000000b, 0
	.quad	0x584d4d000000000c, 0
	.quad	0x584d4d000000000d, 0
	.quad	0x584d4d000000000e, 0
	.quad	0x584d4d000000000f, 0

	.text

	.globl	ms_cross_probe
ms_cross_probe:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$216, %rsp
	movq	%rcx, 192(%rsp)
	movq	%rdx, 200(%rsp)
	movups	%xmm6,  32(%rsp)
	movups	%xmm7,  48(%rsp)
	movups	%xmm8,  64(%rsp)
	movups	%xmm9,  80(%rsp)
	movups	%xmm10, 96(%rsp)
	movups	%xmm11, 112(%rsp)
	movups	%xmm12, 128(%rsp)
	movups	%xmm13, 144(%rsp)
	movups	%xmm14, 160(%rsp)
	movups	%xmm15, 176(%rsp)
	leaq	xmm_seed(%rip), %r10
	movq	0(%r10),  %xmm6
	movq	16(%r10), %xmm7
	movq	32(%r10), %xmm8
	movq	48(%r10), %xmm9
	movq	64(%r10), %xmm10
	movq	80(%r10), %xmm11
	movq	96(%r10), %xmm12
	movq	112(%r10),%xmm13
	movq	128(%r10),%xmm14
	movq	144(%r10),%xmm15
	movabsq	$0x4d53000000000001, %rbx
	movabsq	$0x4d53000000000002, %rbp
	movabsq	$0x4d53000000000003, %rsi
	movabsq	$0x4d53000000000004, %rdi
	movabsq	$0x4d53000000000005, %r12
	movabsq	$0x4d53000000000006, %r13
	movabsq	$0x4d53000000000007, %r14
	movabsq	$0x4d53000000000008, %r15
	movq	200(%rsp), %rcx
	movq	192(%rsp), %r10
	call	*%r10
	xorl	%eax, %eax
	movabsq	$0x4d53000000000001, %r10
	cmpq	%r10, %rbx
	je	1f
	orl	$1, %eax
1:	movabsq	$0x4d53000000000002, %r10
	cmpq	%r10, %rbp
	je	2f
	orl	$2, %eax
2:	movabsq	$0x4d53000000000003, %r10
	cmpq	%r10, %rsi
	je	3f
	orl	$4, %eax
3:	movabsq	$0x4d53000000000004, %r10
	cmpq	%r10, %rdi
	je	4f
	orl	$8, %eax
4:	movabsq	$0x4d53000000000005, %r10
	cmpq	%r10, %r12
	je	5f
	orl	$16, %eax
5:	movabsq	$0x4d53000000000006, %r10
	cmpq	%r10, %r13
	je	6f
	orl	$32, %eax
6:	movabsq	$0x4d53000000000007, %r10
	cmpq	%r10, %r14
	je	7f
	orl	$64, %eax
7:	movabsq	$0x4d53000000000008, %r10
	cmpq	%r10, %r15
	je	8f
	orl	$128, %eax
8:
	leaq	xmm_seed(%rip), %r11
	movq	%xmm6, %r10
	cmpq	0(%r11), %r10
	je	11f
	orl	$256, %eax
11:	movq	%xmm7, %r10
	cmpq	16(%r11), %r10
	je	12f
	orl	$512, %eax
12:	movq	%xmm8, %r10
	cmpq	32(%r11), %r10
	je	13f
	orl	$1024, %eax
13:	movq	%xmm9, %r10
	cmpq	48(%r11), %r10
	je	14f
	orl	$2048, %eax
14:	movq	%xmm10, %r10
	cmpq	64(%r11), %r10
	je	15f
	orl	$4096, %eax
15:	movq	%xmm11, %r10
	cmpq	80(%r11), %r10
	je	16f
	orl	$8192, %eax
16:	movq	%xmm12, %r10
	cmpq	96(%r11), %r10
	je	17f
	orl	$16384, %eax
17:	movq	%xmm13, %r10
	cmpq	112(%r11), %r10
	je	18f
	orl	$32768, %eax
18:	movq	%xmm14, %r10
	cmpq	128(%r11), %r10
	je	19f
	orl	$65536, %eax
19:	movq	%xmm15, %r10
	cmpq	144(%r11), %r10
	je	20f
	orl	$131072, %eax
20:
	movups	32(%rsp),  %xmm6
	movups	48(%rsp),  %xmm7
	movups	64(%rsp),  %xmm8
	movups	80(%rsp),  %xmm9
	movups	96(%rsp),  %xmm10
	movups	112(%rsp), %xmm11
	movups	128(%rsp), %xmm12
	movups	144(%rsp), %xmm13
	movups	160(%rsp), %xmm14
	movups	176(%rsp), %xmm15
	addq	$216, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rdi
	popq	%rsi
	popq	%rbx
	popq	%rbp
	ret

	.globl	sysv_cross_probe
sysv_cross_probe:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$216, %rsp
	movq	%rcx, 192(%rsp)
	movq	%rdx, 200(%rsp)
	movups	%xmm6,  32(%rsp)
	movups	%xmm7,  48(%rsp)
	movups	%xmm8,  64(%rsp)
	movups	%xmm9,  80(%rsp)
	movups	%xmm10, 96(%rsp)
	movups	%xmm11, 112(%rsp)
	movups	%xmm12, 128(%rsp)
	movups	%xmm13, 144(%rsp)
	movups	%xmm14, 160(%rsp)
	movups	%xmm15, 176(%rsp)
	movabsq	$0x5356420000000001, %rbx
	movabsq	$0x5356420000000002, %rbp
	movabsq	$0x5356420000000003, %r12
	movabsq	$0x5356420000000004, %r13
	movabsq	$0x5356420000000005, %r14
	movabsq	$0x5356420000000006, %r15
	movq	200(%rsp), %rdi
	movq	192(%rsp), %r10
	call	*%r10
	xorl	%eax, %eax
	movabsq	$0x5356420000000001, %r10
	cmpq	%r10, %rbx
	je	31f
	orl	$1, %eax
31:	movabsq	$0x5356420000000002, %r10
	cmpq	%r10, %rbp
	je	32f
	orl	$2, %eax
32:	movabsq	$0x5356420000000003, %r10
	cmpq	%r10, %r12
	je	33f
	orl	$4, %eax
33:	movabsq	$0x5356420000000004, %r10
	cmpq	%r10, %r13
	je	34f
	orl	$8, %eax
34:	movabsq	$0x5356420000000005, %r10
	cmpq	%r10, %r14
	je	35f
	orl	$16, %eax
35:	movabsq	$0x5356420000000006, %r10
	cmpq	%r10, %r15
	je	36f
	orl	$32, %eax
36:
	movups	32(%rsp),  %xmm6
	movups	48(%rsp),  %xmm7
	movups	64(%rsp),  %xmm8
	movups	80(%rsp),  %xmm9
	movups	96(%rsp),  %xmm10
	movups	112(%rsp), %xmm11
	movups	128(%rsp), %xmm12
	movups	144(%rsp), %xmm13
	movups	160(%rsp), %xmm14
	movups	176(%rsp), %xmm15
	addq	$216, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rdi
	popq	%rsi
	popq	%rbx
	popq	%rbp
	ret

/*
 * The negative controls. Each destroys every register a callee of its
 * convention must preserve and returns, so the probe's mask has to light every
 * bit. Only %rsp is left alone: a callee that destroys the stack pointer does
 * not return to be measured, while %rbp is an ordinary callee-saved register a
 * frameless function can clobber and still return through %rsp. These are
 * assembly for the same reason the probes are -- a C body
 * would save and restore the callee-saved set in its own prologue and epilogue
 * and the control would pass for the wrong reason.
 */
	.globl	elfsysv_leaky_ms
elfsysv_leaky_ms:
	xorq	%rbx, %rbx
	xorq	%rbp, %rbp
	xorq	%rsi, %rsi
	xorq	%rdi, %rdi
	xorq	%r12, %r12
	xorq	%r13, %r13
	xorq	%r14, %r14
	xorq	%r15, %r15
	pxor	%xmm6, %xmm6
	pxor	%xmm7, %xmm7
	pxor	%xmm8, %xmm8
	pxor	%xmm9, %xmm9
	pxor	%xmm10, %xmm10
	pxor	%xmm11, %xmm11
	pxor	%xmm12, %xmm12
	pxor	%xmm13, %xmm13
	pxor	%xmm14, %xmm14
	pxor	%xmm15, %xmm15
	ret

	.globl	elfsysv_leaky_sysv
elfsysv_leaky_sysv:
	xorq	%rbx, %rbx
	xorq	%rbp, %rbp
	xorq	%r12, %r12
	xorq	%r13, %r13
	xorq	%r14, %r14
	xorq	%r15, %r15
	ret
