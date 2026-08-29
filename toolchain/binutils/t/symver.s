/* Two bodies under one name at two version nodes. This is the shape WP-53
   has to produce for real and the shape the whole project exists to reach,
   rehearsed here at the smallest size that still exercises the machinery:
   .symver in the assembler, a version script in the linker, and a
   .gnu.version_d that readelf can print.

   The names are glibc's on purpose. A test that passed for foo@V1 and failed
   for memcpy@GLIBC_2.2.5 would be a test of the wrong thing. */

	.text

	.globl		old_memcpy
	.type		old_memcpy, @function
old_memcpy:
	movq		%rdi, %rax
	ret
	.size		old_memcpy, .-old_memcpy

	.globl		new_memcpy
	.type		new_memcpy, @function
new_memcpy:
	movq		%rdi, %rax
	ret
	.size		new_memcpy, .-new_memcpy

	.symver		old_memcpy, memcpy@GLIBC_2.2.5
	.symver		new_memcpy, memcpy@@GLIBC_2.14
