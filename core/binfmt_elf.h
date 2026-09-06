/*
 * binfmt_elf.h -- load a static ET_EXEC or ET_DYN onto the substrate.
 *
 * The loader maps each PT_LOAD through the substrate's as_map, lays the file
 * bytes down, zeroes the .bss tail past p_filesz, and reports what the initial
 * stack's auxiliary vector needs: the entry, and where the program headers came
 * to rest in the user address space.
 */
#ifndef CORE_BINFMT_ELF_H
#define CORE_BINFMT_ELF_H

#include <stddef.h>
#include <stdint.h>

#include "substrate.h"

struct load_info {
	uint64_t entry;		/* e_entry, adjusted by the load bias */
	uint64_t phdr_va;	/* user address of the program headers */
	uint64_t phent;		/* e_phentsize */
	uint64_t phnum;		/* e_phnum */
	uint64_t load_base;	/* lowest mapped user address */
	uint64_t load_end;	/* one past the highest mapped user address */
};

/*
 * Parse and map the ELF image (its whole file, `len` bytes) onto s.  Returns 0
 * with *out filled, or a negative value if the image is not a static x86-64 ELF
 * this loader accepts or a mapping fails.
 */
int elf_load(struct substrate *s, const void *image, size_t len,
	     struct load_info *out);

/* The name /proc/self/maps shows for the image's segments: the program's
 * Linux path.  Set before elf_load. */
void elf_set_image_name(const char *name);

#endif /* CORE_BINFMT_ELF_H */
