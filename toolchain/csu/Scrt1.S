/* Process entry for a position-independent image.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   The same protocol as crt1.S; read that file first.  What differs is one
   line, and it is the line that matters more than it looks: main is reached
   through the GOT rather than by a direct call, so the image can be loaded at
   an address the linker did not choose.

   That matters here beyond the usual reasons.  Spike 2 measured a non-PIE
   image asking for 4 MB at 0x400000 and being refused twenty times in twenty
   inside a Cygwin process, because Windows satisfies a based-anywhere
   allocation out of the lowest free region and the runtime has already
   allocated by then.  A PIE lets the loader put the image where there is
   room, which is why every el8 binary measured in spike/vendor-image-shape/
   is ET_DYN and why this file is the one most programs will use.  */

	.text
	.globl	_start
	.type	_start, @function
_start:
	endbr64
	xorl	%ebp, %ebp

	movq	(%rsp), %rdi
	leaq	8(%rsp), %rsi
	leaq	16(%rsp,%rdi,8), %rdx

	movq	%rsp, %r12

	andq	$-16, %rsp

	call	*main@GOTPCREL(%rip)

	movl	%eax, %edi
	call	*exit@GOTPCREL(%rip)

	ud2
	.size	_start, .-_start

	.section .note.GNU-stack, "", @progbits
