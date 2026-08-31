/* WP-27: the context-transparent face for the setjmp family.
 *
 * The generic int face is sub, call, add, ret.  That shape is correct for
 * every body that runs and returns once, and wrong for a body that captures
 * its caller's context: setjmp stores %rsp as-is and the return address at
 * (%rsp), so through a call-style face it captures the face's own frame.
 * The longjmp that later restores that context resumes inside a face
 * instance whose stack has been reused -- the caller-return slot at the
 * captured frame's top holds whatever return address the caller's later
 * calls pushed there -- and the face's ret follows it, landing after the
 * wrong call site.  This is the old rule that setjmp must not be wrapped
 * in an out-of-line wrapper, met again at the face seam.
 *
 * So the family gets a face with no frame at all: shuffle the two integer
 * argument registers and jump.  The body then sees the ELF caller's true
 * %rsp and return address, captures those, and the restoring jump lands in
 * the caller directly -- no face instance alive on either path.
 *
 * The jump hands the body a System V caller's stack: no shadow space above
 * the return address, the caller's own frame there instead.  That is safe
 * for exactly this family because the bodies are gendef's hand-written
 * assembly (winsup/cygwin/scripts/gendef, sub longjmp): they never touch
 * the caller's shadow area, building their own aligned argument space for
 * the calls they make.  A compiled Microsoft body holds no such promise --
 * it may home its argument registers in the shadow area and would scribble
 * the ELF caller's locals -- so this face is curated to the assembly
 * family in ctx.tsv and is not a general shape.  See the
 * context-transparent-faces decision record.
 *
 * At entry %rsp is 8 mod 16, the same misalignment a Microsoft call site
 * produces, so the body's own alignment arithmetic is undisturbed.  Both
 * argument slots move unconditionally; a one-argument body never reads the
 * second, the int face's headroom argument over again.
 */

	.macro	sv2ms_ctx_face face, body
	.text
	.globl	\face
	.def	\face; .scl 2; .type 32; .endef
	.seh_proc \face
\face:
	.seh_endprologue
	movq	%rsi, %rdx
	movq	%rdi, %rcx
	jmp	\body
	.seh_endproc
	.endm
