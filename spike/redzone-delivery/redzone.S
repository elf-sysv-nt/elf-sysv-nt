/*
 * redzone.S -- the parts of spike 7 a compiler must not be allowed to write.
 *
 * Two watchers and one delivery stub. The watchers are leaves that make no
 * call at all, for the reason spike 3 spelled out: the moment a watcher calls
 * anything, the return address lands eight bytes below %rsp, inside the
 * hundred and twenty-eight the psABI reserves, and the watcher has clobbered
 * what it came to watch. redzone_watch paints the region and reads it back, as
 * in spike 3; redzone_accum keeps a running value in one red-zone slot and
 * folds it forward every round, so that a delivery which scribbles that slot
 * corrupts a computation and not merely a byte.
 *
 * deliver_stub is the frame the driver places by hand. The driver suspends the
 * watcher, points its %rip here and its %rsp at the stack pointer it chose --
 * the interrupted one for the naive construction, 128 below it for the repair,
 * or an alternate stack -- and resumes. The stub marks %rsp-8 first, the way
 * Cygwin's sigdelayed does, then runs the C handler and spins until the driver
 * restores the saved context, which is this model's sigreturn.
 *
 * Cygwin's default convention is Microsoft x64, so every routine here is
 * entered MS-ABI and the first argument arrives in %rcx. The struct rz offsets
 * below are duplicated in redzone.c, where static assertions hold the two
 * copies to each other.
 *
 *   0 stop    8 rounds   16 depth   24 checks   32 failures  40 first_bad
 *  48 shallowest  56 deepest  64 got  72 rsp  80 running  88 acc
 */

	.data
	.align	8
	.globl	handler_calls
handler_calls:
	.quad	0			/* the C handler bumps this: it ran */
	.globl	handler_done
handler_done:
	.quad	0			/* the stub bumps this after it returns */

	.text

/*
 * void redzone_watch(struct rz *w)
 *
 * Paints w->depth bytes below its own %rsp with a pattern encoding each word's
 * distance from %rsp, then reads them back until told to stop, counting what
 * changed and repainting it. It makes no call, so nothing it does can account
 * for a change; anything below %rsp that moves came from outside.
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
	movq	%r8, 64(%rbx)		/* what stood there instead */
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
	 * reports an intact red zone has either found one or gone blind, and
	 * from outside those are the same zero. This writes one word into its
	 * own region every round, and the quiet case has to go red against it
	 * before the quiet case is believed. */
	movq	$0, -8(%r12)
#endif
	movq	8(%rbx), %rax		/* rounds; zero means until stopped */
	testq	%rax, %rax
	jz	.Luntil
	movq	24(%rbx), %r10
	cmpq	%rax, %r10
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
 * void redzone_accum(struct rz *w)
 *
 * The integrity watcher. It keeps a single running value in the red-zone slot
 * at %rsp-8 -- the first byte of the red zone, the very word spike 3 watched
 * die -- reloading it from memory every round, folding it forward, and storing
 * it back. Nothing lives in a register across rounds, so the final value
 * depends only on the round count and the seed unless that slot was written
 * from outside. A reserved delivery lands at offset 136 and never reaches it,
 * so the value survives; a naive one takes %rsp-8 first and overwrites it, and
 * the computation diverges. It makes no call, for the same reason as above.
 *
 * The recurrence and its constants are mirrored in redzone.c, which computes
 * the value the loop must reach and compares.
 */
	.globl	redzone_accum
redzone_accum:
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
	xorq	%r13, %r13		/* the checksum, carried in a saved reg */
	movabsq	$0x2545f4914f6cdd1d, %r14	/* the multiplier */
	movabsq	$0x123456789abcdef1, %r15	/* the increment */
	movq	$1, %rax
	movq	%rax, 80(%rbx)		/* running */

	/*
	 * Each round derives a value from the round number alone, commits it to
	 * the red-zone word, drops every register copy, waits out a window, and
	 * then reads the word back and folds it into the checksum. Across that
	 * window the red zone is the only place the value exists, so a delivery
	 * that writes the word corrupts the value the checksum will fold -- and
	 * cannot be healed by a surviving register, because the store came from
	 * the round number and not from a value kept live. The driver aims each
	 * delivery at the window between the store and the read, whose bounds are
	 * exported below.
	 */
.LaLoop:
	movq	24(%rbx), %rax		/* round number = checks */
	imulq	%r14, %rax
	addq	%r15, %rax		/* value = round*K + C */
	movq	%rax, -8(%r12)		/* commit it to the red zone */
	/* The window the driver aims a delivery at: from here, where the value
	 * lives only in the red-zone word, to just before it is read back. A
	 * delivery caught inside it is one arriving while a leaf relies on its
	 * red zone, which is the case the red zone exists to protect and the one
	 * a reservation has to keep intact. The bounds are exported so the driver
	 * can land there deterministically rather than hope a hijack does. */
	.globl	redzone_accum_win_lo
redzone_accum_win_lo:
	xorq	%rax, %rax		/* forget it: the red zone is sole carrier */
	movl	$400, %ecx		/* hold the window open a while */
.LaSpin:
	decl	%ecx
	jnz	.LaSpin
	.globl	redzone_accum_win_hi
redzone_accum_win_hi:
	movq	-8(%r12), %rax		/* read the value back from the red zone */
	xorq	%rax, %r13		/* fold it into the checksum */
	incq	24(%rbx)		/* checks */
	movq	8(%rbx), %r10		/* rounds; zero means until stopped */
	testq	%r10, %r10
	jz	.LaUntil
	movq	24(%rbx), %r11
	cmpq	%r10, %r11
	jb	.LaLoop
	jmp	.LaDone
.LaUntil:
	cmpq	$0, 0(%rbx)		/* stop */
	je	.LaLoop
.LaDone:
	movq	%r13, 88(%rbx)		/* the checksum it finished with */
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
 * deliver_stub -- the handler frame, entered by the driver's SetThreadContext.
 *
 * On entry %rsp is the stack pointer the driver chose and every general
 * register still holds the interrupted watcher's value; the driver changed
 * only %rip and %rsp. The stub owes nothing to those registers because the
 * driver restores the whole saved context afterwards, which is this model's
 * sigreturn, so it is free to clobber. It writes at %rsp-8 first -- the word
 * Cygwin's sigdelayed takes before it builds anything -- then aligns, gives
 * the C handler its shadow space, calls it, records that it returned, and
 * spins until the driver lifts it out.
 */
	.globl	deliver_stub
deliver_stub:
	movabsq	$0x00c0ffeedead5100, %rax	/* a marker no paint word equals */
	pushq	%rax			/* mark %rsp-8, as sigdelayed does */
	andq	$-16, %rsp		/* align the stack for the call */
	subq	$32, %rsp		/* the shadow space MS x64 owes a callee */
	call	handler_c
	lock	incq handler_done(%rip)	/* the handler ran and returned */
.Lspin:
	pause
	jmp	.Lspin
