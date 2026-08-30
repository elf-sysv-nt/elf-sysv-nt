/*
 * callback_probe.S -- the register-integrity caller WP-23's test cannot let a
 * compiler write, the three System V callbacks it hands down, and two leaky
 * stand-ins that make the check prove itself. The method is spike 3's, narrowed
 * to the one direction WP-23 owns.
 *
 * cb_ms_probe stands in for Windows holding a callback pointer. Windows will not
 * say whether the pointer preserved the Microsoft callee-saved set, so this
 * poisons that whole set itself -- rbx, rbp, rsi, rdi, r12 through r15 and xmm6
 * through xmm15 -- calls the pointer Microsoft x64 with the callback's arguments
 * in Microsoft's registers, and reports one bit per register that came back
 * changed. Zero is the only good answer. It also returns the callee's own return
 * value, so the test can confirm the arguments reached the System V side in the
 * right registers rather than only that nothing leaked.
 *
 * cb_sysv_comparator, cb_sysv_threadproc and cb_sysv_exfilter are the callbacks
 * the ELF world hands down. Each is System V, does the small real work its shape
 * implies from arguments delivered in System V registers, and then destroys
 * rsi, rdi and xmm6 through xmm15 -- the set a System V callee is entitled to
 * treat as scratch and a Microsoft caller is entitled to find intact. That
 * destruction is deliberate: it is what a leak is made of, so a trampoline that
 * fails to bracket the call leaks it and a trampoline that brackets it does not,
 * and the difference is visible rather than asserted. They are assembly, not C,
 * because a C body would restore in its epilogue whatever the compiler chose to
 * save and the clobber would not survive to be measured.
 *
 * cb_leaky_tramp_comparator is a trampoline with its bracketing removed: it
 * shuffles the arguments Microsoft-to-System-V and calls the callback, but does
 * not save or restore the Microsoft set around the call. It is the negative
 * control the round trip leans on -- the callback's clobber leaks through it, so
 * the probe lights exactly the bits the real trampoline keeps dark, which proves
 * the save and restore is the load-bearing part rather than incidental.
 * cb_leaky_ms destroys the whole set outright, the coarser control that proves
 * the probe can fail at all.
 *
 * Compiled -mno-red-zone with the unit; nothing here relies on a red zone.
 */

	.data
	.align	16
cb_xmm_seed:
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
	/* The values a leaking callback writes into the scratch set. Distinct
	 * from the seeds above so a leak is a changed value, not a coincidence. */
	.align	16
cb_xmm_leak:
	.quad	0x4c45414b00000006, 0
	.quad	0x4c45414b00000007, 0
	.quad	0x4c45414b00000008, 0
	.quad	0x4c45414b00000009, 0
	.quad	0x4c45414b0000000a, 0
	.quad	0x4c45414b0000000b, 0
	.quad	0x4c45414b0000000c, 0
	.quad	0x4c45414b0000000d, 0
	.quad	0x4c45414b0000000e, 0
	.quad	0x4c45414b0000000f, 0

	.text

/*
 * void cb_ms_probe(void *fn, const void *a, const void *b,
 *		    uint32_t *gpr_mask, uint32_t *xmm_mask, uint64_t *retval)
 *
 * Entered Microsoft x64 from the C test. GPR mask: rbx, rbp, rsi, rdi, r12-r15
 * in bits 0-7. XMM mask: xmm6-xmm15 in bits 0-9, low 64 bits.
 */
	.globl	cb_ms_probe
cb_ms_probe:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$248, %rsp
	/*   0..31   shadow space for the call to fn
	 *  32..191  xmm6..xmm15 save
	 * 192 fn  200 a  208 b  216 gpr_mask  224 xmm_mask  232 retval
	 * Entry rsp was 8 mod 16; eight pushes keep that; 248 brings it to 0.
	 * The 5th and 6th incoming args sat at entry rsp+40 and rsp+48, now at
	 * rsp+352 and rsp+360. */
	movq	%rcx, 192(%rsp)
	movq	%rdx, 200(%rsp)
	movq	%r8,  208(%rsp)
	movq	%r9,  216(%rsp)
	movq	352(%rsp), %rax
	movq	%rax, 224(%rsp)
	movq	360(%rsp), %rax
	movq	%rax, 232(%rsp)

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

	leaq	cb_xmm_seed(%rip), %r10
	movq	0(%r10),   %xmm6
	movq	16(%r10),  %xmm7
	movq	32(%r10),  %xmm8
	movq	48(%r10),  %xmm9
	movq	64(%r10),  %xmm10
	movq	80(%r10),  %xmm11
	movq	96(%r10),  %xmm12
	movq	112(%r10), %xmm13
	movq	128(%r10), %xmm14
	movq	144(%r10), %xmm15
	movabsq	$0x4d53000000000001, %rbx
	movabsq	$0x4d53000000000002, %rbp
	movabsq	$0x4d53000000000003, %rsi
	movabsq	$0x4d53000000000004, %rdi
	movabsq	$0x4d53000000000005, %r12
	movabsq	$0x4d53000000000006, %r13
	movabsq	$0x4d53000000000007, %r14
	movabsq	$0x4d53000000000008, %r15

	movq	200(%rsp), %rcx		/* a -> first MS argument */
	movq	208(%rsp), %rdx		/* b -> second MS argument */
	movq	192(%rsp), %r10		/* fn */
	call	*%r10

	movq	232(%rsp), %r10		/* retval out */
	movq	%rax, (%r10)

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
8:	movq	216(%rsp), %r10
	movl	%eax, (%r10)

	xorl	%eax, %eax
	leaq	cb_xmm_seed(%rip), %r11
	movq	%xmm6, %r10
	cmpq	0(%r11), %r10
	je	11f
	orl	$1, %eax
11:	movq	%xmm7, %r10
	cmpq	16(%r11), %r10
	je	12f
	orl	$2, %eax
12:	movq	%xmm8, %r10
	cmpq	32(%r11), %r10
	je	13f
	orl	$4, %eax
13:	movq	%xmm9, %r10
	cmpq	48(%r11), %r10
	je	14f
	orl	$8, %eax
14:	movq	%xmm10, %r10
	cmpq	64(%r11), %r10
	je	15f
	orl	$16, %eax
15:	movq	%xmm11, %r10
	cmpq	80(%r11), %r10
	je	16f
	orl	$32, %eax
16:	movq	%xmm12, %r10
	cmpq	96(%r11), %r10
	je	17f
	orl	$64, %eax
17:	movq	%xmm13, %r10
	cmpq	112(%r11), %r10
	je	18f
	orl	$128, %eax
18:	movq	%xmm14, %r10
	cmpq	128(%r11), %r10
	je	19f
	orl	$256, %eax
19:	movq	%xmm15, %r10
	cmpq	144(%r11), %r10
	je	20f
	orl	$512, %eax
20:	movq	224(%rsp), %r10
	movl	%eax, (%r10)

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
	addq	$248, %rsp
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
 * The System V callbacks the ELF world hands down. Each reads its arguments
 * from System V registers, does the small real work its shape implies, then
 * destroys the scratch set -- rsi, rdi, xmm6-xmm15 -- which a System V callee
 * may and a Microsoft caller may not lose. r10 and r11 are volatile in both
 * conventions, so they are the only temporaries used.
 */
	.macro	CB_CLOBBER_SCRATCH
	movabsq	$0x4c45414b00000003, %rsi
	movabsq	$0x4c45414b00000004, %rdi
	leaq	cb_xmm_leak(%rip), %r11
	movq	0(%r11),   %xmm6
	movq	16(%r11),  %xmm7
	movq	32(%r11),  %xmm8
	movq	48(%r11),  %xmm9
	movq	64(%r11),  %xmm10
	movq	80(%r11),  %xmm11
	movq	96(%r11),  %xmm12
	movq	112(%r11), %xmm13
	movq	128(%r11), %xmm14
	movq	144(%r11), %xmm15
	.endm

/* int32_t cb_sysv_comparator(const void *a, const void *b): a in rdi, b in rsi.
 * Returns *(int32_t*)a - *(int32_t*)b, a valid comparator ordering. */
	.globl	cb_sysv_comparator
cb_sysv_comparator:
	movl	(%rdi), %eax
	movl	(%rsi), %r10d
	subl	%r10d, %eax
	CB_CLOBBER_SCRATCH
	ret

/* uint32_t cb_sysv_threadproc(void *arg): arg in rdi. Returns *(uint32_t*)arg,
 * the thread's exit code, so the argument's delivery is observable. */
	.globl	cb_sysv_threadproc
cb_sysv_threadproc:
	movl	(%rdi), %eax
	CB_CLOBBER_SCRATCH
	ret

/* int32_t cb_sysv_exfilter(void *ep): ep in rdi. Returns *(int32_t*)ep, the
 * disposition the filter reads out of the pointer it was handed. */
	.globl	cb_sysv_exfilter
cb_sysv_exfilter:
	movl	(%rdi), %eax
	CB_CLOBBER_SCRATCH
	ret

/*
 * cb_leaky_tramp_comparator -- a trampoline with the bracketing taken out. It
 * shuffles the arguments Microsoft-to-System-V and calls the callback, and does
 * not save or restore the Microsoft callee-saved set around the call, so the
 * callback's clobber leaks straight through to the Microsoft caller. The real
 * trampoline differs from this in exactly the save and restore, so the probe
 * lighting here and staying dark there isolates that as the load-bearing part.
 */
	.globl	cb_leaky_tramp_comparator
cb_leaky_tramp_comparator:
	subq	$8, %rsp		/* align rsp for the System V call */
	movq	%rcx, %rdi		/* MS arg 1 -> System V arg 1 */
	movq	%rdx, %rsi		/* MS arg 2 -> System V arg 2 */
	call	cb_sysv_comparator
	addq	$8, %rsp
	ret

/*
 * cb_leaky_ms -- the coarse control. Destroys every register a Microsoft callee
 * must preserve and returns, so the probe's masks have to light every bit; it
 * proves the check can fail before a dark mask from the real trampoline is
 * believed. rsp is left alone, and rbp with it, for the reason WP-22's controls
 * leave them: a callee that loses the stack does not return to be measured.
 */
	.globl	cb_leaky_ms
cb_leaky_ms:
	xorq	%rbx, %rbx
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
