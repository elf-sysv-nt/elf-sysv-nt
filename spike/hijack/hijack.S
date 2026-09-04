/*
 * hijack.S -- the parts of the hijack probe a compiler must not write.
 *
 * The question this spike measures is whether a signal can be delivered into a
 * thread that is not cooperating: suspend it, rewrite its context, resume. The
 * targets have to be hand asm for two reasons. A red-zone watcher must make no
 * call, because the instant it does the return address lands eight bytes below
 * %rsp and destroys the very bytes it came to watch -- spike 3's lesson, and
 * DR-0030's. And the in-kernel-flag target needs its critical region to sit
 * between two label addresses the driver can compare a hijacked %rip against,
 * which no compiler will promise to keep.
 *
 * Three routines and a handler frame:
 *   spin_target   a tight user-mode leaf that paints and re-reads its red zone
 *   flag_target   a leaf that raises an in-kernel flag around a marked region
 *   deliver_stub  the handler frame the driver lays down by hand
 *
 * Entered MS x64, so the struct pointer arrives in %rcx. The struct hij offsets
 * below are duplicated in hijack.c, where static assertions hold the two copies
 * to each other.
 *
 *   0 stop    8 running   16 rsp   24 iters   32 depth   40 failures
 *  48 first_bad  56 shallowest  64 deepest  72 got  80 in_kernel
 *  88 crit_lo  96 crit_hi  104 flagiters
 */

	.data
	.align	8
	.globl	handler_calls
handler_calls:
	.quad	0			/* the stub bumps this on entry: it was reached */
	.globl	handler_done
handler_done:
	.quad	0			/* the stub bumps this after the C handler returns */
	.globl	t_handler
t_handler:
	.quad	0			/* rdtsc read by the stub's first instruction */

	.text

/*
 * void spin_target(struct hij *w)
 *
 * Paints w->depth bytes below its own %rsp with a distance-encoded pattern,
 * then reads them back forever, counting what changed and repainting it, until
 * w->stop. It makes no call and no syscall, so it is a thread spinning purely
 * in user mode -- the first case a hijack has to reach -- and anything that
 * moves below %rsp came from outside it.
 */
	.globl	spin_target
spin_target:
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
	movq	%r12, 16(%rbx)
	movq	32(%rbx), %rsi		/* depth, in bytes */
	movabsq	$0x484a4b5a00000000, %r13	/* the paint marker */

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
	movq	$1, 8(%rbx)		/* running */

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
	incq	40(%rbx)		/* failures */
	cmpq	$0, 48(%rbx)		/* first_bad */
	jne	.Lspan
	movq	%rcx, 48(%rbx)
	movq	%r8, 72(%rbx)		/* what stood there instead */
.Lspan:
	movq	56(%rbx), %r10		/* shallowest */
	testq	%r10, %r10
	jz	.Lshallow
	cmpq	%r10, %rcx
	jae	.Ldeep
.Lshallow:
	movq	%rcx, 56(%rbx)
.Ldeep:
	cmpq	64(%rbx), %rcx		/* deepest */
	jbe	.Lrepaint
	movq	%rcx, 64(%rbx)
.Lrepaint:
	movq	%r9, (%rdx)
.Lnext:
	addq	$8, %rcx
	jmp	.Lscan
.Lscanned:
	incq	24(%rbx)		/* iters */
	cmpq	$0, 0(%rbx)		/* stop */
	je	.Lround
	movq	$0, 8(%rbx)
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
 * void flag_target(struct hij *w)
 *
 * Models the gate's in-kernel flag. Each pass raises w->in_kernel, spins
 * through a critical region bounded by crit_lo and crit_hi, lowers the flag,
 * then spins an equal while with the flag down. The bounds are exported so the
 * driver, after it suspends the thread, can ask whether the %rip it caught lies
 * inside the region the flag claims to guard -- the whole point of the flag.
 */
	.globl	flag_target
flag_target:
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	movq	%rcx, %rbx		/* w */
	leaq	.Lcrit_lo(%rip), %rax
	movq	%rax, 88(%rbx)
	leaq	.Lcrit_hi(%rip), %rax
	movq	%rax, 96(%rbx)
	movq	$1, 8(%rbx)		/* running */

.Lfloop:
	movq	$1, 80(%rbx)		/* in_kernel up, before the region */
.Lcrit_lo:
	movl	$300, %ecx
.Lcrit_spin:
	decl	%ecx
	jnz	.Lcrit_spin
.Lcrit_hi:
	movq	$0, 80(%rbx)		/* in_kernel down, after the region */
	movl	$300, %ecx
.Lnc_spin:
	decl	%ecx
	jnz	.Lnc_spin
	incq	104(%rbx)		/* flagiters */
	cmpq	$0, 0(%rbx)		/* stop */
	je	.Lfloop
	movq	$0, 8(%rbx)
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret

/*
 * deliver_stub -- the handler frame, entered by the driver's context rewrite.
 *
 * On entry %rsp is the stack pointer the driver chose -- the interrupted one
 * for the naive control, 128 below it for the reserved delivery -- and every
 * register still holds the interrupted thread's value; the driver changed only
 * %rip and %rsp, and restores the whole saved context afterwards, which is this
 * model's sigreturn. The first instruction stamps rdtsc so the driver can price
 * the delivery, marks the word at the chosen %rsp-8 the way sigdelayed does,
 * then gives the C handler its shadow space and calls it.
 */
	.globl	deliver_stub
deliver_stub:
	rdtsc
	shlq	$32, %rdx
	orq	%rdx, %rax
	movq	%rax, t_handler(%rip)
	lock	incq handler_calls(%rip)
	pushq	%rax			/* mark the chosen %rsp-8, below any red zone */
	andq	$-16, %rsp
	subq	$32, %rsp		/* the shadow space MS x64 owes a callee */
	call	handler_c
	lock	incq handler_done(%rip)
.Lstub_spin:
	pause
	jmp	.Lstub_spin
