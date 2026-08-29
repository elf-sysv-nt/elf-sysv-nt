/* The two psABI TLS access sequences a linker is allowed to rewrite, written
   by hand because there is no cross compiler yet to emit them.

   Byte-for-byte from the AMD64 psABI's TLS chapter, prefixes included. The
   prefixes are not decoration: ld identifies the sequence by them and needs
   the padding to rewrite in place, so a sequence written without them is not
   the sequence the linker will meet. */

	.section	.tbss, "awT", @nobits
	.globl		tlsvar
	.align		8
tlsvar:
	.zero		8

	.text
	.globl		_start
	.type		_start, @function
_start:
	/* general dynamic */
	.byte		0x66
	leaq		tlsvar@tlsgd(%rip), %rdi
	.value		0x6666
	rex64
	call		__tls_get_addr@PLT

	/* initial exec */
	movq		tlsvar@gottpoff(%rip), %rax
	movq		%fs:(%rax), %rax

	ret
	.size		_start, .-_start
