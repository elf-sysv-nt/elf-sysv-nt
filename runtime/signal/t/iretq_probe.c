/* Probe: is a same-privilege iretq legal in user mode on this host, and does it
 * set rsp from the frame rather than from the pops? Prints two bits. */
#include <stdio.h>

extern long probe_iret_same(void);
extern long probe_iret_moved(void);

__asm__(
".text\n"
".globl probe_iret_same\n"
"probe_iret_same:\n"
"	movq	%rsp, %r10\n"
"	movq	%cs, %rax\n"
"	movq	%ss, %r8\n"
"	pushfq\n"
"	popq	%r9\n"
"	leaq	1f(%rip), %rdx\n"
"	pushq	%r8\n"
"	pushq	%r10\n"
"	pushq	%r9\n"
"	pushq	%rax\n"
"	pushq	%rdx\n"
"	iretq\n"
"1:	xorl	%eax, %eax\n"
"	cmpq	%r10, %rsp\n"
"	sete	%al\n"
"	ret\n"
".globl probe_iret_moved\n"
"probe_iret_moved:\n"
"	movq	%rsp, %r10\n"
"	leaq	-512(%r10), %r11\n"	/* land 512 below, then walk back */
"	movq	%cs, %rax\n"
"	movq	%ss, %r8\n"
"	pushfq\n"
"	popq	%r9\n"
"	leaq	2f(%rip), %rdx\n"
"	pushq	%r8\n"
"	pushq	%r11\n"
"	pushq	%r9\n"
"	pushq	%rax\n"
"	pushq	%rdx\n"
"	iretq\n"
"2:	xorl	%eax, %eax\n"
"	cmpq	%r11, %rsp\n"
"	sete	%al\n"
"	movq	%r10, %rsp\n"
"	ret\n");

int main(void)
{
	long a = probe_iret_same();
	long b = probe_iret_moved();
	printf("iretq_same=%ld iretq_moved=%ld\n", a, b);
	return (a && b) ? 0 : 1;
}
