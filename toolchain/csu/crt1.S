/* Process entry for a statically linked image.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   Entered by a jump with %rsp on argc, which is what the psABI says a process
   entry point is entered with and what WP-40 builds.  Not a call: there is no
   return address at (%rsp), and a crt that assumed one would read argc as a
   pointer.

   The frame pointer is zeroed first so that an unwinder walking back from
   here stops rather than following whatever the stack happened to hold.  Every
   backtrace in every program on this platform ends at this instruction, which
   is worth two bytes.  */

	.text
	.globl	_start
	.type	_start, @function
_start:
	endbr64
	xorl	%ebp, %ebp

	/* argc, argv, envp, in the System V argument registers.  envp sits one
	   past argv's null terminator, which is why the scale-index reaches
	   16 rather than 8.  */
	movq	(%rsp), %rdi
	leaq	8(%rsp), %rsi
	leaq	16(%rsp,%rdi,8), %rdx

	/* The auxv the runtime will want later starts past envp's terminator.
	   Parked in a register the callee must preserve rather than recomputed,
	   because __libc_start_main is where it goes once WP-53 exists and
	   nothing before then should have to find it twice.  */
	movq	%rsp, %r12

	/* The psABI wants %rsp 16-byte aligned at the call, and the entry
	   protocol does not promise it.  */
	andq	$-16, %rsp

	call	main

	/* main's return value is the exit status, and it leaves through exit,
	   not _exit: glibc's startup returns from main into exit, which runs
	   the atexit chain and flushes stdio, and a program whose buffered
	   output vanishes at return from main is observably not that platform.
	   The string-slice differential caught exactly that.  */
	movl	%eax, %edi
	call	exit

	/* exit does not return.  If it does, fault here rather than run off
	   into whatever follows: a crt that falls through is how a program
	   ends up executing its own .rodata.  */
	ud2
	.size	_start, .-_start

	/* Mark the stack non-executable.  Absent this, ld gives the image an
	   executable PT_GNU_STACK and WP-32 honours it, which would hand every
	   program on the platform a writable executable mapping.  */
	.section .note.GNU-stack, "", @progbits
