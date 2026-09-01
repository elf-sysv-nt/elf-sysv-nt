/*
 * The init-crossing specimen's entry. Same exit protocol as the WP-41 and
 * dynamic-crossing specimens: no syscall is made, the host hands the
 * termination handler in %rdx, and the program leaves through it with the
 * status in %rdi. Here the status is init_body()'s return -- the global the
 * image's initializers wrote -- so a process exit of 42 is reached only when
 * the loader ran the DT_INIT chain, in order, before this entry.
 */
	.bss
	.align 8
saved_handler:
	.quad 0

	.text
	.globl entry
entry:
	/* %rdx is the host's termination handler; save it before init_body's
	 * prologue can touch it. */
	movq	%rdx, saved_handler(%rip)
	call	init_body		/* %eax is the status the initializers left */
	movzbl	%al, %edi
	movq	saved_handler(%rip), %rax
	jmp	*%rax

	.section .note.GNU-stack,"",@progbits
