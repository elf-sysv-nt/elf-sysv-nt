/* WP-34: the lazy-binding resolver trampoline.
 *
 * A mapped object left to bind lazily has GOT[2] pointing here and GOT[1]
 * holding the cookie -- the elf_reloc_object* the engine stored. On the first
 * call through a PLT slot the PLT pushes the relocation index and the cookie
 * and jumps here with the caller's real argument registers still live. This
 * trampoline is the bridge glibc's _dl_runtime_resolve is, with one added
 * crossing: the mapped world runs the System V ABI, but this trampoline and the
 * fixup it calls are host (Cygwin) code on the Microsoft ABI, so the fixup's
 * arguments go in %rcx and %rdx and the call needs 32 bytes of shadow space.
 *
 * On entry the System V stack is:
 *     0(%rsp)  cookie   (GOT[1], the elf_reloc_object*)
 *     8(%rsp)  index    (the relocation index the PLT pushed)
 *    16(%rsp)  return address into the caller
 *
 * The System V caller-clobbered registers that may carry live arguments are
 * saved across the fixup and restored, the resolved target is taken in %r11,
 * the cookie and index are dropped, and control tail-jumps to the target with
 * the caller's return address back on top.
 *
 *    uint64_t elf_reloc_fixup(elf_reloc_object *cookie, uint64_t index);
 */
	.text
	.globl	elf_reloc_runtime_resolve
	.p2align 4
elf_reloc_runtime_resolve:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$224, %rsp		/* 32 shadow + 128 xmm + 56 gp + 8 pad */

	/* save the System V argument / scratch registers */
	movq	%rdi, 160(%rsp)
	movq	%rsi, 168(%rsp)
	movq	%rdx, 176(%rsp)
	movq	%rcx, 184(%rsp)
	movq	%r8,  192(%rsp)
	movq	%r9,  200(%rsp)
	movq	%rax, 208(%rsp)
	movdqu	%xmm0, 32(%rsp)
	movdqu	%xmm1, 48(%rsp)
	movdqu	%xmm2, 64(%rsp)
	movdqu	%xmm3, 80(%rsp)
	movdqu	%xmm4, 96(%rsp)
	movdqu	%xmm5, 112(%rsp)
	movdqu	%xmm6, 128(%rsp)
	movdqu	%xmm7, 144(%rsp)

	/* fixup(cookie, index) in the Microsoft ABI */
	movq	8(%rbp), %rcx		/* cookie */
	movq	16(%rbp), %rdx		/* index */
	call	elf_reloc_fixup
	movq	%rax, %r11		/* resolved target */

	/* restore the saved registers */
	movdqu	32(%rsp),  %xmm0
	movdqu	48(%rsp),  %xmm1
	movdqu	64(%rsp),  %xmm2
	movdqu	80(%rsp),  %xmm3
	movdqu	96(%rsp),  %xmm4
	movdqu	112(%rsp), %xmm5
	movdqu	128(%rsp), %xmm6
	movdqu	144(%rsp), %xmm7
	movq	160(%rsp), %rdi
	movq	168(%rsp), %rsi
	movq	176(%rsp), %rdx
	movq	184(%rsp), %rcx
	movq	192(%rsp), %r8
	movq	200(%rsp), %r9
	movq	208(%rsp), %rax

	movq	%rbp, %rsp
	popq	%rbp
	addq	$16, %rsp		/* drop cookie and index */
	jmp	*%r11			/* tail-call the resolved target */
