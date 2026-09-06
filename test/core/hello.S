/*
 * hello.S -- the static test program the core runs, hand-written to the gate
 * ABI (Core-Phase1.md D1).  A stock el8 binary reaches the kernel with the
 * `syscall` instruction, which substrate N cannot trap (spike 1); this one
 * reads the gate's address out of the auxiliary vector as AT_SYSINFO and
 * `call`s it, which is the door N does open.  It writes "hello\n" to fd 1 and
 * exits 0 -- nothing else, so what the core has to get right is exactly the
 * increment's bar and no incidental libc.
 *
 * Register convention at the gate is the kernel's: number in %rax, arguments in
 * %rdi %rsi %rdx %r10 %r8 %r9, result in %rax.  The gate preserves every
 * register but %rax, %rcx and %r11, so the gate address is kept in %r15 across
 * both calls.
 *
 * Build: x86_64-elfsysvnt-linux-gnu-gcc -nostdlib -static -no-pie (the triple is
 * doc/design/target-definition.md's).
 */
	.text
	.globl	_start
_start:
	/* The substrate enters a fresh thread as if through a call: the return
	 * slot is at [%rsp] and argc one word above it.  Step past the slot to
	 * the psABI's entry stack, where %rsp points at argc. */
	leaq	8(%rsp), %rsp

	movq	(%rsp), %rax		/* argc */
	leaq	16(%rsp,%rax,8), %rdi	/* &envp[0] = past argc, argv, argv NULL */

	/* Walk envp to its null terminator; the auxv begins just after. */
1:	movq	(%rdi), %rcx
	addq	$8, %rdi
	testq	%rcx, %rcx
	jnz	1b

	/* Scan the auxv for AT_SYSINFO (32); its value is the gate. */
2:	movq	(%rdi), %rax		/* a_type */
	testq	%rax, %rax
	je	3f			/* AT_NULL: no gate, cannot proceed */
	cmpq	$32, %rax
	je	4f
	addq	$16, %rdi
	jmp	2b

4:	movq	8(%rdi), %r15		/* the gate address */

	/* write(1, msg, 6) */
	movq	$1, %rdi
	leaq	msg(%rip), %rsi
	movq	$6, %rdx
	movq	$1, %rax
	call	*%r15

	/* exit_group(0) -- does not return */
	xorl	%edi, %edi
	movq	$231, %rax
	call	*%r15

3:	ud2				/* unreached: no gate in the auxv */

	.section .rodata
msg:
	.ascii	"hello\n"
