/*
 * The dynamic-crossing certification's main image: bzip2's shape at the size
 * the harness can run without a libc. An interp-bearing, no-PIE ET_EXEC that
 * imports one function from an ELF runtime and calls it across the object
 * boundary through its PLT. Entered raw — the way WP-41 enters a static image —
 * it would fault on that call, because its GOT points at nothing yet; only
 * after the stub's dynamic branch has run dyn_exec_link over the pair does the
 * PLT resolve into the runtime. So reaching the runtime's function at all is
 * the crossing working.
 *
 * The exit protocol is the WP-41 specimen's, unchanged: no syscall is made,
 * because the syscall surface is a later package. The host hands the
 * termination handler's address in %rdx and the program leaves through it with
 * the status in %rdi. The entry stub captures %rdx before any call can clobber
 * it, calls dyn_body() — the C body whose one statement is the cross-object
 * call, giving the image the unwind tables and section layout a real program
 * carries — and leaves with the byte the call returned. A process exit of 42
 * (greet()'s sentinel) is reached only when the cross-object call the crossing
 * linked actually ran and returned.
 */

	.bss
	.align 8
saved_handler:
	.quad 0

	.text
	.globl entry
entry:
	/* %rdx is the host's termination handler; save it before dyn_body's
	 * prologue can touch it. */
	movq	%rdx, saved_handler(%rip)
	call	dyn_body		/* the cross-object call; %eax is the status */
	movzbl	%al, %edi
	movq	saved_handler(%rip), %rax
	jmp	*%rax

	.section .note.GNU-stack,"",@progbits
