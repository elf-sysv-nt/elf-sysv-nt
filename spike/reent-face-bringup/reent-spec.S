/*
 * The reent-face-bringup live run's main image -- item 3's specimen.
 *
 * dyn-cross-spec.S's shape, carried one rung further: an interp-bearing,
 * no-PIE ET_EXEC that imports its work from an ELF runtime through its PLT and
 * leaves with the call's result as the process status. Where dyn-cross imports
 * a sentinel greet() from a bare libgreet.so, this imports a reent-consuming
 * body's ingredients -- strtol and __errno -- from the WP-53 libc.so.6 veneer,
 * whose thunks resolve into the elfsysv1.dll face the loader maps and names
 * through AT_BASE. So entered raw it would fault (its GOT points at nothing);
 * only after the stub's dynamic crossing links its PLT against the veneer, and
 * the veneer's constructor reads AT_BASE, does the call reach the face.
 *
 * Exit protocol is WP-41's, unchanged: no syscall. The host hands the
 * termination handler in %rdx; the entry stub saves it before reent_body's
 * prologue can touch it, calls reent_body() -- the C body whose statements are
 * the reent-consuming cross-object calls -- and leaves through the handler with
 * the byte the body returned (42 is the witness; see reent-body.c).
 */

	.bss
	.align 8
saved_handler:
	.quad 0

	.text
	.globl entry
entry:
	/* %rdx is the host's termination handler; save it before reent_body's
	 * prologue can touch it. */
	movq	%rdx, saved_handler(%rip)
	call	reent_body		/* the reent-consuming crossing; %eax is the status */
	movzbl	%al, %edi
	movq	saved_handler(%rip), %rax
	jmp	*%rax

	.section .note.GNU-stack,"",@progbits
