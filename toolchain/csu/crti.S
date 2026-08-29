/* The opening half of _init and _fini.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   These two files are a matched pair and a link that includes one without the
   other produces a function with a prologue and no return, or a return and no
   prologue.  The linker will not notice.  crti.o goes first on the link line
   and crtn.o last, with everything else between; the compiler driver does that
   for you and this is the file that explains why it must.

   The subq is not bookkeeping.  Whatever the linker drops between these two
   halves is a sequence of calls, and the psABI wants %rsp 16-byte aligned at
   each one.  Eight bytes here restores the alignment the entry to _init
   consumed with its return address.  */

	.section .init, "ax", @progbits
	.globl	_init
	.type	_init, @function
_init:
	endbr64
	subq	$8, %rsp

	.section .fini, "ax", @progbits
	.globl	_fini
	.type	_fini, @function
_fini:
	endbr64
	subq	$8, %rsp

	.section .note.GNU-stack, "", @progbits
