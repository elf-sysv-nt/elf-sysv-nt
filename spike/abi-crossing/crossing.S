/*
 * crossing.S -- the parts of spike 3 a compiler must not be allowed to write.
 *
 * Three routines. Two of them are callers that speak one convention on
 * purpose and then check that the other side kept its half of the bargain.
 * They are hand-written because a compiler asked to make these calls would
 * emit the very thunk it is being measured for, and a check expressed in
 * terms of the thing it checks measures nothing.
 *
 * The third is the red-zone watcher, and it is hand-written for a harde
 * reason: it has to be a leaf that makes no call at all. The moment it calls
 * anything, the return address lands eight bytes below %rsp, inside the
 * hundred and twenty-eight the psABI reserves, and the watcher has clobbered
 * what it came to watch.
 *
 * Cygwin's default convention is Microsoft x64, so every routine here is
 * entered MS-ABI from crossing.c and every C function in this spike is MS-ABI
 * unless it carries the sysv_abi attribute.
 */

	.data
	.align	16
ms_seed:
	.quad	0x584d4d0000000006	/* xmm6  */
	.quad	0x584d4d0000000007	/* xmm7  */
	.quad	0x584d4d0000000008	/* xmm8  */
	.quad	0x584d4d0000000009	/* xmm9  */
	.quad	0x584d4d000000000a	/* xmm10 */
	.quad	0x584d4d000000000b	/* xmm11 */
	.quad	0x584d4d000000000c	/* xmm12 */
	.quad	0x584d4d000000000d	/* xmm13 */
	.quad	0x584d4d000000000e	/* xmm14 */
	.quad	0x584d4d000000000f	/* xmm15 */

	.text

/*
 * uint64_t sysv_caller_probe(void *fn, const struct sysv_args *a,
 *			      uint64_t *retval)
 *
 * Enters MS-ABI, leaves as a System V caller. Six integer arguments in
 * %rdi, %rsi, %rdx, %rcx, %r8, %r9; eight doubles in %xmm0 through %xmm7;
 * two more integers on the stack; %al carrying the vector-register count the
 * variadic convention wants and every other callee is entitled to ignore.
 *
 * Before the call it puts a sentinel in each of the six registers System V
 * calls callee-saved, and after it returns a bit for each one that came back
 * changed: %rbx, %rbp, %r12, %r13, %r14, %r15 in bits 0 through 5. Zero is
 * the only good answer.
 *
 * Nothing here touches %rbp after the sentinel goes in, so every local is
 * reached through %rsp.
 */
	.globl	sysv_caller_probe
sysv_caller_probe:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$88, %rsp
	/*   0(%rsp), 8(%rsp)  the two outgoing stack arguments
	 *  16(%rsp)           fn
	 *  24(%rsp)           the argument block
	 *  32(%rsp)           where the return value goes
	 * Entry %rsp is 8 mod 16, eight pushes leave that alone, and 88 more
	 * brings it to 0 mod 16, which is what System V wants at the call. */
	movq	%rcx, 16(%rsp)
	movq	%rdx, 24(%rsp)
	movq	%r8, 32(%rsp)

	movq	%rdx, %rax
	movq	112(%rax), %rcx
	movq	%rcx, 0(%rsp)
	movq	120(%rax), %rcx
	movq	%rcx, 8(%rsp)
	movsd	48(%rax), %xmm0
	movsd	56(%rax), %xmm1
	movsd	64(%rax), %xmm2
	movsd	72(%rax), %xmm3
	movsd	80(%rax), %xmm4
	movsd	88(%rax), %xmm5
	movsd	96(%rax), %xmm6
	movsd	104(%rax), %xmm7
	movq	0(%rax), %rdi
	movq	8(%rax), %rsi
	movq	16(%rax), %rdx
	movq	32(%rax), %r8
	movq	40(%rax), %r9
	movq	24(%rax), %rcx		/* last: %rcx was the temporary */

	movq	16(%rsp), %r10
	movabsq	$0x5356420000000001, %rbx
	movabsq	$0x5356420000000002, %rbp
	movabsq	$0x5356420000000003, %r12
	movabsq	$0x5356420000000004, %r13
	movabsq	$0x5356420000000005, %r14
	movabsq	$0x5356420000000006, %r15
	movl	$8, %eax
	call	*%r10

	movq	32(%rsp), %r10
	movq	%rax, (%r10)

	xorl	%eax, %eax
	movabsq	$0x5356420000000001, %r10
	cmpq	%r10, %rbx
	je	1f
	orl	$1, %eax
1:	movabsq	$0x5356420000000002, %r10
	cmpq	%r10, %rbp
	je	2f
	orl	$2, %eax
2:	movabsq	$0x5356420000000003, %r10
	cmpq	%r10, %r12
	je	3f
	orl	$4, %eax
3:	movabsq	$0x5356420000000004, %r10
	cmpq	%r10, %r13
	je	4f
	orl	$8, %eax
4:	movabsq	$0x5356420000000005, %r10
	cmpq	%r10, %r14
	je	5f
	orl	$16, %eax
5:	movabsq	$0x5356420000000006, %r10
	cmpq	%r10, %r15
	je	6f
	orl	$32, %eax
6:	addq	$88, %rsp
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
 * void ms_caller_probe(void *fn, const struct ms_args *a, uint64_t *retval,
 *			uint32_t *gpr_mask, uint32_t *xmm_mask)
 *
 * The other direction, and the one the design is actually nervous about.
 * Windows calls into the runtime constantly -- DllMain, thread starts, APCs,
 * the fault machinery -- and each of those callers is entitled to assume the
 * Microsoft callee-saved set survives. That set is wider than System V's: it
 * adds %rsi, %rdi and the ten volatile-under-System-V registers %xmm6 through
 * %xmm15. If any of those leaks out through a call into System V code, the
 * convention has escaped through the bottom of the DLL.
 *
 * Windows will not tell us whether it came back intact, so this stands in fo
 * Windows: a Microsoft-convention caller that poisons all eighteen registers,
 * makes the call, and reports what changed. The GPR mask is %rbx, %rbp, %rsi,
 * %rdi, %r12 through %r15 in bits 0 through 7; the XMM mask is %xmm6 through
 * %xmm15 in bits 0 through 9, low sixty-four bits only.
 *
 * The callee is entered as ms_face(uint64_t, double, uint64_t, double,
 * uint64_t, double), which puts arguments in %rcx, %xmm1, %r8, %xmm3 and two
 * on the stack, because Microsoft assigns by position rather than by class
 * and an all-integer signature would not exercise that.
 */
	.macro	CHKGPR reg, sentinel, bit
	movabsq	$\sentinel, %r10
	cmpq	%r10, \reg
	je	9f
	orl	$\bit, %eax
9:
	.endm

	.macro	CHKXMM xreg, off, bit
	movq	\xreg, %rcx
	cmpq	\off(%r11), %rcx
	je	9f
	orl	$\bit, %eax
9:
	.endm

	.globl	ms_caller_probe
ms_caller_probe:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$104, %rsp
	/*   0(%rsp)..31       the shadow space Microsoft x64 owes its callee
	 *  32(%rsp), 40(%rsp) the two outgoing stack arguments
	 *  48(%rsp)           fn
	 *  56(%rsp)           the argument block
	 *  64(%rsp)           where the return value goes
	 *  72(%rsp)           where the GPR mask goes
	 *  80(%rsp)           where the XMM mask goes
	 * The fifth incoming argument sat at entry %rsp + 40; eight pushes and
	 * 104 bytes of frame put it at %rsp + 208. */
	movq	%rcx, 48(%rsp)
	movq	%rdx, 56(%rsp)
	movq	%r8, 64(%rsp)
	movq	%r9, 72(%rsp)
	movq	208(%rsp), %rax
	movq	%rax, 80(%rsp)

	movq	56(%rsp), %rax
	movq	32(%rax), %r10
	movq	%r10, 32(%rsp)
	movq	40(%rax), %r10
	movq	%r10, 40(%rsp)
	movq	0(%rax), %rcx
	movsd	8(%rax), %xmm1
	movq	16(%rax), %r8
	movsd	24(%rax), %xmm3

	leaq	ms_seed(%rip), %r10
	movq	0(%r10), %xmm6
	movq	8(%r10), %xmm7
	movq	16(%r10), %xmm8
	movq	24(%r10), %xmm9
	movq	32(%r10), %xmm10
	movq	40(%r10), %xmm11
	movq	48(%r10), %xmm12
	movq	56(%r10), %xmm13
	movq	64(%r10), %xmm14
	movq	72(%r10), %xmm15
	movabsq	$0x4d53000000000001, %rbx
	movabsq	$0x4d53000000000002, %rbp
	movabsq	$0x4d53000000000003, %rsi
	movabsq	$0x4d53000000000004, %rdi
	movabsq	$0x4d53000000000005, %r12
	movabsq	$0x4d53000000000006, %r13
	movabsq	$0x4d53000000000007, %r14
	movabsq	$0x4d53000000000008, %r15

	movq	48(%rsp), %r10
	call	*%r10

	movq	64(%rsp), %r10
	movq	%rax, (%r10)

	xorl	%eax, %eax
	CHKGPR	%rbx, 0x4d53000000000001, 1
	CHKGPR	%rbp, 0x4d53000000000002, 2
	CHKGPR	%rsi, 0x4d53000000000003, 4
	CHKGPR	%rdi, 0x4d53000000000004, 8
	CHKGPR	%r12, 0x4d53000000000005, 16
	CHKGPR	%r13, 0x4d53000000000006, 32
	CHKGPR	%r14, 0x4d53000000000007, 64
	CHKGPR	%r15, 0x4d53000000000008, 128
	movq	72(%rsp), %r10
	movl	%eax, (%r10)

	xorl	%eax, %eax
	leaq	ms_seed(%rip), %r11
	CHKXMM	%xmm6, 0, 1
	CHKXMM	%xmm7, 8, 2
	CHKXMM	%xmm8, 16, 4
	CHKXMM	%xmm9, 24, 8
	CHKXMM	%xmm10, 32, 16
	CHKXMM	%xmm11, 40, 32
	CHKXMM	%xmm12, 48, 64
	CHKXMM	%xmm13, 56, 128
	CHKXMM	%xmm14, 64, 256
	CHKXMM	%xmm15, 72, 512
	movq	80(%rsp), %r10
	movl	%eax, (%r10)

	xorl	%eax, %eax
	addq	$104, %rsp
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
 * void redzone_watch(struct rz *w)
 *
 * Paints the w->depth bytes below its own %rsp with a pattern that encodes
 * each word's distance from %rsp, then reads them back forever, counting what
 * changed and repainting it. It makes no call, so nothing it does can account
 * for a change; anything that shows up came from outside -- a signal, an
 * exception, an APC, the kernel.
 *
 * The pattern encodes the offset rather than being a constant so that a word
 * moved from elsewhere in the region is distinguishable from a word
 * overwritten in place.
 *
 * With w->mode set to 1 the loop also faults once a round, by storing through
 * a zeroed %r11, and the two exported labels let the exception handler in
 * crossing.c step over it by name. Stepping over it by instruction length
 * would work today and break the first time the encoding changes.
 *
 * Offsets into struct rz are duplicated in crossing.c, where static
 * assertions hold the two copies to each other.
 */
	.globl	redzone_watch
redzone_watch:
	pushq	%rbp
	pushq	%rbx
	pushq	%rsi
	pushq	%rdi
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rcx, %rbx		/* w */
	movq	%rsp, %r12		/* the %rsp under watch */
	movq	%r12, 72(%rbx)
	movq	16(%rbx), %rsi		/* depth, in bytes */
	xorq	%rdi, %rdi		/* rounds done */
	movabsq	$0x525a4f4e00000000, %r13

	movq	$8, %rcx
.Lpaint:
	cmpq	%rsi, %rcx
	ja	.Lpainted
	movq	%r12, %rdx
	subq	%rcx, %rdx
	movq	%r13, %rax
	addq	%rcx, %rax
	movq	%rax, (%rdx)
	addq	$8, %rcx
	jmp	.Lpaint
.Lpainted:
	movq	$1, %rax
	movq	%rax, 80(%rbx)		/* running */

.Lround:
	movq	$8, %rcx
.Lscan:
	cmpq	%rsi, %rcx
	ja	.Lscanned
	movq	%r12, %rdx
	subq	%rcx, %rdx
	movq	(%rdx), %r8
	movq	%r13, %r9
	addq	%rcx, %r9
	cmpq	%r9, %r8
	je	.Lnext
	incq	32(%rbx)		/* failures */
	cmpq	$0, 40(%rbx)		/* first_bad */
	jne	.Lspan
	movq	%rcx, 40(%rbx)
	movq	%r8, 64(%rbx)		/* and what stood there instead */
.Lspan:
	movq	48(%rbx), %r10		/* shallowest */
	testq	%r10, %r10
	jz	.Lshallow
	cmpq	%r10, %rcx
	jae	.Ldeep
.Lshallow:
	movq	%rcx, 48(%rbx)
.Ldeep:
	cmpq	56(%rbx), %rcx		/* deepest */
	jbe	.Lrepaint
	movq	%rcx, 56(%rbx)
.Lrepaint:
	movq	%r9, (%rdx)
.Lnext:
	addq	$8, %rcx
	jmp	.Lscan
.Lscanned:
	incq	24(%rbx)		/* checks */
#ifdef SPIKE_REDZONE_SELF_CLOBBER
	/* The negative control, built only by t/run-tests.sh. A watcher that
	 * reports an intact red zone has either found one or lost the ability
	 * to notice, and from outside those look the same. This writes one
	 * word into the region on every round, and the quiet case has to go
	 * red against it before the quiet case is believed. */
	movq	$0, -8(%r12)
#endif
	cmpq	$1, 88(%rbx)		/* mode */
	jne	.Lcount
	incq	96(%rbx)		/* faults */
	xorl	%r11d, %r11d
	.globl	redzone_fault_at
redzone_fault_at:
	movl	$1, (%r11)
	.globl	redzone_fault_resume
redzone_fault_resume:
.Lcount:
	incq	%rdi
	movq	8(%rbx), %rax		/* rounds; zero means until stopped */
	testq	%rax, %rax
	jz	.Luntil
	cmpq	%rax, %rdi
	jb	.Lround
	jmp	.Ldone
.Luntil:
	cmpq	$0, 0(%rbx)		/* stop */
	je	.Lround
.Ldone:
	movq	$0, 80(%rbx)
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
 * uint64_t leaky_face(void)
 *
 * The negative control for both register checks, and it is never called
 * except by the build t/run-tests.sh makes. It returns having destroyed every
 * register a callee of either convention is obliged to preserve, so the masks
 * have to come back lit. %rbp and %rsp are left alone: a callee that destroys
 * those does not return to report anything, which would test the harness
 * rather than the check.
 *
 * This has to be assembly. Inline asm inside the C faces would be undone on
 * the way out, because gcc restores in its epilogue whatever it saved in its
 * prologue, and the control would then pass for the wrong reason.
 */
	.globl	leaky_face
leaky_face:
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
	xorl	%eax, %eax
	ret
