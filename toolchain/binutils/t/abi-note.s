/* The .note.ABI-tag exactly as doc/target-definition.md fixes it, and as el8
   emits it: owner GNU, type NT_GNU_ABI_TAG, Linux, minimum kernel 3.2.0.

   Hand-written because the startup files that will carry it belong to WP-14
   and do not exist yet. What this proves at WP-12 is narrower than what
   WP-14 will prove: that the section survives assembly and linking, lands in
   a PT_NOTE, and reads back byte-identical to the vendor's. */

	.section	.note.ABI-tag, "a", @note
	.p2align	2
	.long		4		/* n_namesz, "GNU" and its terminator */
	.long		16		/* n_descsz, four words */
	.long		1		/* NT_GNU_ABI_TAG */
	.asciz		"GNU"
	.p2align	2
	.long		0		/* ELF_NOTE_OS_LINUX */
	.long		3
	.long		2
	.long		0
