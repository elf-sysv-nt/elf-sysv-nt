/*
 * redzone.S -- a System V leaf that keeps live bytes in its red zone and then
 * faults, so that whatever NT writes below %rsp while dispatching the fault
 * lands where a compiled leaf's temporaries would be.
 *
 * Entered MS x64: %rcx is the address to touch, %rdx is where the first
 * clobbered offset (distance below %rsp, 1..128) is stored, %r8d says which
 * fault to take. Returns the number of clobbered bytes in %rax. Makes no call
 * and pushes nothing: the 128 bytes below %rsp are painted with their own
 * offsets, the fault is taken, and the paint is read back. Anything that
 * changed was written by the dispatch, since nothing here writes there twice.
 *
 *   kind 0   a store to the page in %rcx (uncommitted: an access violation the
 *            vectored handler resolves by committing, and the store re-executes)
 *   kind 1   int3 (a breakpoint the handler steps over)
 *   kind 2   no fault at all (the control: the same store to a committed page)
 */
	.text
	.globl	redzone_leaf
redzone_leaf:
	/* paint: byte at rsp-i holds i, for i = 1..128 */
	movq	$128, %r9
1:	movb	%r9b, %r10b
	negq	%r9
	movb	%r10b, (%rsp,%r9)
	negq	%r9
	decq	%r9
	jnz	1b

	/* the event */
	cmpl	$1, %r8d
	je	2f
	movq	$0x5a5a5a5a5a5a5a5a, %rax
	movq	%rax, (%rcx)		/* kind 0 faults here; kind 2 does not */
	jmp	3f
2:	int3

	/* read back */
3:	xorq	%rax, %rax		/* clobbered count */
	movq	$0, (%rdx)
	movq	$128, %r9
4:	negq	%r9
	movb	(%rsp,%r9), %r10b
	negq	%r9
	cmpb	%r10b, %r9b
	je	5f
	incq	%rax
	cmpq	$0, (%rdx)
	jne	5f
	movq	%r9, (%rdx)		/* first clobbered offset, counting down from 128 */
5:	decq	%r9
	jnz	4b
	ret
