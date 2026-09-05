/*
 * hello-oracle.S -- the same logical program as hello.S, for a real kernel.
 *
 * Core-Phase1.md D2 makes Rocky 8 a behavioural oracle rather than a
 * bit-for-bit one: substrate N cannot trap `syscall` (spike 1), so the program
 * that runs under the core reaches the kernel by calling the gate, and no
 * single binary can be both.  This is the other half of that pair.  Same two
 * syscalls, same arguments, same message, same order -- reached through the
 * `syscall` instruction, which is the door Linux opens.
 *
 * What the comparison is worth depends on this file staying the twin of
 * hello.S.  Change one and change the other, or the oracle stops answering the
 * question it was asked.  The differences are meant to be exactly two: no auxv
 * walk, since there is no gate address to find, and `syscall` where hello.S has
 * `call *%r15`.
 *
 * Build: x86_64-elfsysvnt-linux-gnu-gcc -nostdlib -static -no-pie.  The cross
 * toolchain targets Linux, so what it emits here runs unmodified on el8.
 */
	.text
	.globl	_start
_start:
	/* write(1, msg, 6) */
	movq	$1, %rdi
	leaq	msg(%rip), %rsi
	movq	$6, %rdx
	movq	$1, %rax
	syscall

	/* exit_group(0) -- does not return */
	xorl	%edi, %edi
	movq	$231, %rax
	syscall

	ud2				/* unreached: exit_group does not return */

	.section .rodata
msg:
	.ascii	"hello\n"
