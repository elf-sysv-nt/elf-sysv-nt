/* An ifunc, which exists here for one reason: it is the trigger that makes a
   linker write ELFOSABI_GNU instead of ELFOSABI_NONE. doc/target-definition.md
   says the byte describes the object rather than the platform, and this is
   the object that makes it say so. */

	.text

	.type		chosen, @function
chosen:
	movq		%rdi, %rax
	ret
	.size		chosen, .-chosen

	.type		resolve, @function
resolve:
	leaq		chosen(%rip), %rax
	ret
	.size		resolve, .-resolve

	.globl		dispatch
	.type		dispatch, @gnu_indirect_function
	.set		dispatch, resolve
