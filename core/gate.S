/*
 * gate.S -- the gate entry.  User code (Linux, gate-calling) reaches this by a
 * plain `call` through the AT_SYSINFO address, with the Linux syscall register
 * convention: number in %rax, arguments in %rdi %rsi %rdx %r10 %r8 %r9, result
 * in %rax.  The job here is threefold: get off the user's small stack onto the
 * kernel stack before running any core frame, capture the call into a sysframe
 * for the C dispatcher, and put everything back the way a returning syscall
 * leaves it -- every register but %rax, %rcx and %r11 unchanged, %rsp restored,
 * and control back at the user return address.
 *
 * gate_dispatch is compiled for the host's Win64 ABI, so it is called with the
 * frame pointer in %rcx over 32 bytes of shadow space, not in the Linux
 * argument registers.  Handing it one pointer keeps that remapping out of the
 * hot path and out of this file's arithmetic.
 */
	.text
	.globl	gate_entry
gate_entry:
	movq	%rsp, %r11			/* r11 = user rsp (ret addr at [r11]) */
	movq	g_kstack_top(%rip), %rsp	/* switch onto the kernel stack */

	pushq	%r11				/* save user rsp */
	/* Build the sysframe, high argument first so nr lands at the low
	 * address: after these seven pushes %rsp points at nr. */
	pushq	%r9				/* a6 */
	pushq	%r8				/* a5 */
	pushq	%r10				/* a4 */
	pushq	%rdx				/* a3 */
	pushq	%rsi				/* a2 */
	pushq	%rdi				/* a1 */
	pushq	%rax				/* nr  -> %rsp = &sysframe */

	movq	%rsp, %rcx			/* Win64 arg1 = &sysframe */
	subq	$32, %rsp			/* shadow space, stays 16-aligned */
	call	gate_dispatch
	addq	$32, %rsp			/* drop shadow; %rsp = &sysframe */

	/* Result is in %rax.  Restore the argument registers the syscall
	 * convention preserves, straight from the sysframe that saved them. */
	movq	8(%rsp), %rdi			/* a1 */
	movq	16(%rsp), %rsi			/* a2 */
	movq	24(%rsp), %rdx			/* a3 */
	movq	32(%rsp), %r10			/* a4 */
	movq	40(%rsp), %r8			/* a5 */
	movq	48(%rsp), %r9			/* a6 */

	addq	$56, %rsp			/* pop the seven sysframe words */
	popq	%r11				/* user rsp */
	movq	%r11, %rsp			/* back onto the user stack */
	ret					/* return to user; %rax = result */
