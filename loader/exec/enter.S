/*
 * WP-41: the one-way entry.
 *
 * The last thing the stub does. proc_build_stack (WP-40) reports the %rsp to
 * enter on and the %rdx to carry; this sets exactly those and reaches the ELF
 * entry point by jmp, because argc belongs at (%rsp) and a call would put a
 * return address there instead.
 *
 * There is nothing to save. A real exec does not come back -- the image the
 * stub was created to run has replaced it in every sense that matters -- so
 * unlike WP-40's test trampoline this preserves no registers and parks no
 * resume address. If the image returns, it returns onto whatever the kernel's
 * stack layout put above argc, which on Linux is the same nothing.
 *
 * Written in assembly because a stack switch is not expressible in C and a
 * compiler that believes it owns the frame would spill into a stack that is
 * about to stop existing.
 */

	.data
	.align 8
/*
 * The host stack pointer at the moment of the crossing, parked so the way back
 * can restore it.
 *
 * Not a convenience. Cygwin locates its per-thread state from the stack
 * pointer -- the carrier DR-0003 settled on -- so a call into the host's C
 * library made while the ELF stack is in force computes that state's address
 * from an address in the wrong region and faults. Measured here: the
 * certification specimen's exit went through _exit on the ELF stack and died
 * with an access violation, and moved back onto this stack it returns a
 * status. Anything crossing outward has to switch stacks first, and this is
 * where the stack to switch to is kept.
 */
	.globl	elf_host_rsp
elf_host_rsp:
	.quad	0

	.text

/*
 * void elf_enter(void *entry, void *sp, uint64_t rdx)
 *   entry -> %rcx   sp -> %rdx   rdx -> %r8      (Microsoft ABI on the way in)
 *
 * The System V entry contract on the way out: %rsp on argc and 16-byte
 * aligned, %rdx the shared-object termination handler or zero. The remaining
 * registers are cleared rather than left carrying whatever the stub had in
 * them, which is what the kernel does and what a program written to assume it
 * relies on.
 */
	.globl	elf_enter
elf_enter:
	movq	%rcx, %r10		/* the entry, out of the way of the wipe */
	movq	%rsp, elf_host_rsp(%rip)
	movq	%rdx, %rsp		/* switch to the built stack */
	movq	%r8, %rdx		/* the atexit handler for the image */

	xorl	%eax, %eax
	xorl	%ecx, %ecx
	xorl	%ebx, %ebx
	xorl	%ebp, %ebp
	xorl	%esi, %esi
	xorl	%edi, %edi
	xorl	%r8d, %r8d
	xorl	%r9d, %r9d
	xorl	%r11d, %r11d
	xorl	%r12d, %r12d
	xorl	%r13d, %r13d
	xorl	%r14d, %r14d
	xorl	%r15d, %r15d

	jmp	*%r10

/*
 * void elf_terminate(void)   -- entered System V-faced, status in %rdi
 *
 * The value the stub puts in %rdx: the termination handler the psABI reserves
 * that register for. A program built on a startup file registers it and it
 * runs when main returns; a program with no startup file, which is what the
 * certification specimen is, jumps to it directly with a status in %rdi.
 * Either way it is the one way out of the ELF world that does not require the
 * syscall surface a later package delivers.
 *
 * The status crosses from System V's first argument register to the Microsoft
 * ABI's, and the host stack parked by elf_enter is restored before anything
 * else, because the host's C library finds its per-thread state from the stack
 * pointer and cannot be called while the ELF stack is in force.
 */
	.globl	elf_terminate
elf_terminate:
	movq	%rdi, %rcx
	movq	elf_host_rsp(%rip), %rsp
	/* The host ABI wants %rsp 16-byte aligned at the call and thirty-two
	   bytes of shadow space above it. Thirty-two, not forty: the forty a
	   prologue that has already pushed an odd number of words would use
	   leaves %rsp eight past the boundary here, and the first aligned move
	   in the callee faults. That is what the specimen's exit died of
	   before this line was counted rather than copied. */
	andq	$-16, %rsp
	subq	$32, %rsp
	call	elf_terminate_host
	hlt
