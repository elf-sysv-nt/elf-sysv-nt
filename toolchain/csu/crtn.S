/* The closing half of _init and _fini.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   Pairs with crti.S; read the comment there.  This file must be last on the
   link line, after every object that might contribute to .init or .fini.  */

	.section .init, "ax", @progbits
	addq	$8, %rsp
	ret

	.section .fini, "ax", @progbits
	addq	$8, %rsp
	ret

	.section .note.GNU-stack, "", @progbits
