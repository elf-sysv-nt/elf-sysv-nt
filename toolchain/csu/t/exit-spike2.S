/* An _exit that returns to spike 2's stub, for want of a runtime.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../../LICENSE.

   Scaffolding, and deliberately in the test directory rather than beside the
   startup files.  crt1.o calls _exit and does not care who supplies it; on a
   finished platform that is the runtime, reached through the veneer, and
   WP-41 is what gives a process somewhere to go when it ends.  None of that
   exists yet, and WP-14's exit criterion is that a static image links and the
   spike 2 stub runs it, so this supplies the one symbol standing between
   those two facts.

   The leave protocol is spike 2's and belongs to it: the stub parks its own
   stack pointer in a handshake block, passes the block's address in an auxv
   entry of its own invention, and gets control back when the image restores
   that pointer and returns.  A real exec does not return, which is why this
   shape is a spike's and not a platform's.

   Nothing here may be linked into anything shipped.  */

	.set	AT_NULL,		0
	.set	AT_SPIKE_HANDSHAKE,	0x7000

	.set	HS_HOST_RSP,		0x08
	.set	HS_OUT_STATUS,		0x18	/* the stub reads argc here; a
						   status is what we have to
						   report and the slot is the
						   one it prints */

	.text
	/* crt1 now leaves through exit, the flushing door; in this spike there
	   is no stdio to flush and no atexit chain, so exit and _exit are the
	   same symbol here.  */
	.globl	exit
	.set	exit, _exit
	.globl	_exit
	.type	_exit, @function
_exit:
	endbr64
	/* %edi is the status.  %r12 is the entry %rsp, parked by crt1 before
	   it aligned the stack, so argc is at (%r12) and the vectors follow.  */
	movq	(%r12), %rax		/* argc */
	leaq	8(%r12), %rcx		/* argv */
	leaq	8(%rcx,%rax,8), %rcx	/* past argv and its terminator: envp */
1:	movq	(%rcx), %rax
	addq	$8, %rcx
	testq	%rax, %rax
	jnz	1b			/* %rcx is on the first auxv key */

	xorq	%r10, %r10
2:	movq	(%rcx), %rax
	movq	8(%rcx), %rdx
	addq	$16, %rcx
	testq	%rax, %rax
	jz	3f
	cmpq	$AT_SPIKE_HANDSHAKE, %rax
	cmoveq	%rdx, %r10
	jmp	2b
3:
	/* Without the block there is no way to report and no way to leave,
	   since the host's stack pointer is parked inside it.  Spin rather
	   than fault, so the stub's timeout says which of the two went wrong
	   instead of a crash saying neither.  Spike 2's payload does the same
	   and for the same reason.  */
	testq	%r10, %r10
	jz	3b

	movl	%edi, %eax
	movq	%rax, HS_OUT_STATUS(%r10)

	movq	HS_HOST_RSP(%r10), %rsp
	ret
	.size	_exit, .-_exit

	.section .note.GNU-stack, "", @progbits
