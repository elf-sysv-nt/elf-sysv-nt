/* WP-56: the frameless face for the runtime slice's jmp_buf-translating
 * rows -- _setjmp, setjmp, _longjmp, longjmp, siglongjmp.
 *
 * DR-0051 found this family needs the same shape DR-0041 gave the sv2ms
 * seam's setjmp family (sv2ms-ctx.inc): setjmp/longjmp capture and restore
 * the literal calling frame -- %rsp as it stands and the return address at
 * (%rsp) -- not a value handed to them. An ordinary call-style shim that
 * called Cygwin's real setjmp would capture the shim's own frame instead
 * of the ELF caller's; the eventual longjmp would resume inside dead,
 * reused stack. So this face never calls the real body: it shuffles the
 * jmp_buf pointer to the real Cygwin buffer it leads to (allocating that
 * real buffer on first use for the setjmp side) and jmps into the real
 * body through the slice's own bind table, the same table
 * wire-runtime.gen.s's ordinary thunks read. The real body then sees the
 * true ELF caller's %rsp and return address directly.
 *
 * El8's jmp_buf is 64 bytes and opaque to every conforming caller (POSIX
 * only requires it round-trip through the same implementation's own
 * setjmp/longjmp); Cygwin's is 256 bytes on x86_64 (_JBLEN 32,
 * machine/setjmp.h). A translating shim cannot rewrite the buffer in
 * place the way a same-size struct's fields would; it needs an
 * out-of-line real buffer the el8-shaped one leads back to. The chosen
 * home for that pointer is the caller's own buffer, first eight bytes:
 * stashing a pointer there costs nothing a conforming caller can observe,
 * needs no thread-local slot or side table, and a jmp_buf copied by value
 * carries its stashed pointer along for free -- matching, not worsening,
 * POSIX's own silence on what a copied jmp_buf's behaviour should be.
 *
 * wire_jmpbuf_save is the setjmp/_setjmp shape: one argument (the el8
 * jmp_buf pointer, %rdi), lazy allocation when the stashed word reads
 * zero -- a fresh or zero-initialized jmp_buf, or the first pass of the
 * common `while (setjmp(buf))` idiom, which must keep its one real buffer
 * for the loop's whole lifetime, never reallocating on a later pass.
 * malloc is called through the GOT indirection Scrt1.S already uses for
 * an out-of-DSO call from hand-written assembly (toolchain/csu/Scrt1.S);
 * the one push before it and the matching pop after keep %rsp exactly as
 * this face found it -- 8 mod 16 at entry, the same misalignment a
 * standard call site produces -- so the real body's own alignment
 * arithmetic, and the eventual restoring jump, see the ELF caller's
 * frame undisturbed. Only the caller-saved registers (%rax, %rdi and the
 * malloc call's own scratch set) are ever touched; %rbx, %rbp, %r12-%r15
 * -- the registers setjmp actually captures -- are never written, so
 * they still hold the true caller's values when the real body runs.
 *
 * wire_jmpbuf_restore is the longjmp/_longjmp/siglongjmp shape: two
 * arguments (the el8 jmp_buf pointer in %rdi, the return value in %esi),
 * no allocation -- longjmp through a jmp_buf that was never setjmp'd is
 * already undefined behaviour on every implementation, so a null stashed
 * pointer here is a caller bug, not a case this face needs to paper over.
 * The return value crosses untouched in %rsi; only %rdi is rewritten,
 * from the el8 buffer's address to the real Cygwin buffer's, before the
 * tail jump.
 *
 * Both macros take the declaration directive (.globl or .weak) as an
 * argument, matching gen-wire.py's ordinary thunks, whose weak/global
 * split follows the forward map's sbind column rather than a fixed
 * choice -- longjmp's three names bind weak on glibc, setjmp's two bind
 * global. Neither macro emits its own .symver; the generator emits that
 * line itself, once per row, the same way gen-wire.py's thunk loop does.
 *
 * Both macros are a handful of instructions and never build a frame; the
 * jump in either direction lands in the real Cygwin body with the ELF
 * caller's registers exactly as the caller left them, the same property
 * sv2ms-ctx.inc arranges one seam down. See the jmp_buf-frameless-face
 * decision record.
 */

	.macro	wire_jmpbuf_save face, decl, table, off
	.text
	\decl	\face
	.type	\face,@function
\face:
	movq	(%rdi), %rax
	testq	%rax, %rax
	jnz	1f
	pushq	%rdi
	movl	$256, %edi
	call	*malloc@GOTPCREL(%rip)
	popq	%rdi
	movq	%rax, (%rdi)
1:
	movq	%rax, %rdi
	jmp	*\table+\off(%rip)
	.size	\face, .-\face
	.endm

	.macro	wire_jmpbuf_restore face, decl, table, off
	.text
	\decl	\face
	.type	\face,@function
\face:
	movq	(%rdi), %rax
	movq	%rax, %rdi
	jmp	*\table+\off(%rip)
	.size	\face, .-\face
	.endm
