/* WP-27: the generic System V face over an all-integer Microsoft body.
 *
 * A signature whose every parameter and result travels in the integer file
 * needs no arity: the same register move covers a zero-argument body and a
 * ten-argument one, because integer arguments occupy the same positional
 * slots under both conventions and a callee never reads slots its prototype
 * does not name.  One macro therefore faces the whole int class.  The
 * unlisted class does not ride on it by guess: per the face-shapes record
 * its disposition is decided from Cygwin's own declarations when the
 * generator learns to read them, and until then no face is emitted for it.
 *
 * The move, System V in to Microsoft down:
 *
 *   arg  1..4   rdi rsi rdx rcx  ->  rcx rdx r8 r9
 *   arg  5..6   r8  r9           ->  [rsp+0x20] [rsp+0x28]
 *   arg  7..14  [caller stack]   ->  [rsp+0x30] .. [rsp+0x68]
 *
 * Eight stack slots are copied unconditionally.  The widest int-class
 * signature on the surface takes ten arguments (__res_nmkquery), so eight
 * slots leave four of headroom; when the true arity is smaller the copy
 * reads the caller's own frame above its argument area -- mapped stack,
 * since the caller's rsp sat above it at the call -- and the body never
 * looks at slots past its prototype.  Nothing is pushed, so the frame is
 * one sub: 0x20 shadow + 0x50 argument slots + 8 to restore sixteen-byte
 * alignment from the System V call site.
 *
 * Returns need no hand: an integer or pointer result is rax under both
 * conventions.  The Microsoft callee saves a superset of what a System V
 * caller expects preserved (rsi, rdi, xmm6-15 on top of rbx, rbp, r12-15),
 * so the crossing cannot lose a register in this direction.  r10 is the
 * scratch for the slot copy: volatile under both, and not rax, which a
 * System V caller may set for its own purposes on entry.
 *
 * SEH data is emitted so the host can unwind the frame; what an unwind
 * through the face means for the System V frames above it is DR-0012's
 * seam, unchanged here.
 */

	.macro	sv2ms_int_face face, body
	.text
	.globl	\face
	.def	\face; .scl 2; .type 32; .endef
	.seh_proc \face
\face:
	subq	$0x78, %rsp
	.seh_stackalloc 0x78
	.seh_endprologue
	movq	%r9, 0x28(%rsp)
	movq	%r8, 0x20(%rsp)
	movq	%rcx, %r9
	movq	%rdx, %r8
	movq	%rsi, %rdx
	movq	%rdi, %rcx
	movq	0x80(%rsp), %r10
	movq	%r10, 0x30(%rsp)
	movq	0x88(%rsp), %r10
	movq	%r10, 0x38(%rsp)
	movq	0x90(%rsp), %r10
	movq	%r10, 0x40(%rsp)
	movq	0x98(%rsp), %r10
	movq	%r10, 0x48(%rsp)
	movq	0xa0(%rsp), %r10
	movq	%r10, 0x50(%rsp)
	movq	0xa8(%rsp), %r10
	movq	%r10, 0x58(%rsp)
	movq	0xb0(%rsp), %r10
	movq	%r10, 0x60(%rsp)
	movq	0xb8(%rsp), %r10
	movq	%r10, 0x68(%rsp)
	call	\body
	addq	$0x78, %rsp
	ret
	.seh_endproc
	.endm
