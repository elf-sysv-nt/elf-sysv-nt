/* The three TLS access models a linker is allowed to rewrite, one per
   section so that accept.sh can assemble and link each on its own.  A single
   object carrying all three would prove only that the first one it met was
   rewritten, which is the weaker claim and the one a first draft of the bfd
   refusal patch passed while local dynamic still leaked.

   Byte-for-byte from the AMD64 psABI's TLS chapter, prefixes included.  The
   prefixes are not decoration: ld identifies a sequence by them and needs the
   padding to rewrite in place, so a sequence written without them is not the
   sequence the linker will meet.

   Assembled with --defsym MODEL_x=1 to select one.  */

	.section	.tbss, "awT", @nobits
	.globl		tlsvar
	.align		8
tlsvar:
	.zero		8

	.text
	.globl		_start
	.type		_start, @function
_start:

.ifdef MODEL_GD
	.byte		0x66
	leaq		tlsvar@tlsgd(%rip), %rdi
	.value		0x6666
	rex64
	call		__tls_get_addr@PLT
.endif

.ifdef MODEL_LD
	leaq		tlsvar@tlsld(%rip), %rdi
	call		__tls_get_addr@PLT
	movq		tlsvar@dtpoff(%rax), %rax
.endif

.ifdef MODEL_IE
	/* The form this target's compiler emits with -mno-tls-direct-seg-refs:
	   the thread pointer fetched through the carrier, then the GOT word
	   added.  The psABI's movq %fs:(%rax) form is the compiler's to
	   avoid; ld rewrites only the GOT load, to an immediate, either
	   way.  */
	movq		%gs:0x1678, %rax
	addq		tlsvar@gottpoff(%rip), %rax
.endif

.ifdef MODEL_LE
	/* Local exec, which stays legal: TPOFF32 is a value rather than a
	   sequence and the linker writes no instruction bytes for it.  What
	   the compiler does with the value is WP-13's problem, not ld's.  */
	movq		$tlsvar@tpoff, %rax
.endif

	ret
	.size		_start, .-_start
