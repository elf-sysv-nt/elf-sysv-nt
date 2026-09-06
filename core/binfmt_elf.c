/*
 * binfmt_elf.c -- the static-ELF loader.
 *
 * It does what the kernel's binfmt_elf does for a statically linked executable
 * and no more: check the identification, walk the program headers, and for each
 * PT_LOAD realise the pages through the substrate at the segment's protection,
 * copy the file portion, and zero the rest.  A read-only or executable segment
 * ends at its file size; a writable one may run on into .bss, which reads back
 * zero because the pages it lands on were never written.
 *
 * The one place this leans on user_copy_out rather than as_map is the partial
 * page where file data stops and .bss begins: as_map lays whole file pages, so
 * the bytes above p_filesz in that last page are cleared afterwards, the same
 * fix-up the real loader's padzero performs.
 */
#include <string.h>

#include "binfmt_elf.h"
#include "vma.h"

static char image_name[256] = "";

void elf_set_image_name(const char *name)
{
	size_t n = strlen(name);
	if (n >= sizeof image_name) n = sizeof image_name - 1;
	memcpy(image_name, name, n);
	image_name[n] = 0;
}

#define PAGE		4096u
#define ROUND_DN(x)	((uint64_t)(x) & ~(uint64_t)(PAGE - 1))
#define ROUND_UP(x)	ROUND_DN((uint64_t)(x) + PAGE - 1)

/* ELF64, little-endian, x86-64 -- the subset the identification check admits. */
#define ELFCLASS64	2
#define ELFDATA2LSB	1
#define ET_EXEC		2
#define ET_DYN		3
#define EM_X86_64	62
#define PT_LOAD		1
#define PF_X		1
#define PF_W		2
#define PF_R		4

/* A DYN image with no fixed address is placed here, the way the kernel drops a
 * PIE at a chosen base; an EXEC keeps its own absolute addresses (bias 0). */
#define DYN_BASE	0x555555554000ULL

struct elf64_ehdr {
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum;
	uint16_t e_shentsize, e_shnum, e_shstrndx;
};

struct elf64_phdr {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
};

static int prot_of(uint32_t flags)
{
	int p = 0;

	if (flags & PF_R)
		p |= SUB_PROT_READ;
	if (flags & PF_W)
		p |= SUB_PROT_WRITE;
	if (flags & PF_X)
		p |= SUB_PROT_EXEC;
	return p;
}

/* A run of zeroes long enough to clear any single partial page in one copy. */
static const unsigned char zeros[PAGE];

static int map_load(struct substrate *s, const void *image, size_t len,
		    const struct elf64_phdr *ph, uint64_t bias)
{
	uint64_t va = ph->p_vaddr + bias;
	uint64_t map_start = ROUND_DN(va);
	uint64_t delta = va - map_start;
	uint64_t filesz = ph->p_filesz, memsz = ph->p_memsz;
	int prot = prot_of(ph->p_flags);
	struct sub_backing file = { SUB_BACKING_FILE, image, len };
	struct sub_backing anon = { SUB_BACKING_ANON, NULL, 0 };

	if (memsz < filesz)
		return -1;

	/* The file-backed pages: from the segment's page down through the last
	 * page any file byte touches.  as_map copies from the page-aligned file
	 * offset, so the byte at p_offset lands exactly at va. */
	if (filesz > 0) {
		uint64_t file_end = va + filesz;
		size_t maplen = (size_t)(ROUND_UP(file_end) - map_start);
		void *addr = (void *)(uintptr_t)map_start;
		uint64_t foff = ph->p_offset - delta;

		if (s->as_map(s, &addr, maplen, &file, foff, prot) != 0)
			return -1;
		vma_record((uint64_t)(uintptr_t)addr, maplen, prot, SUB_BACKING_FILE, foff, image_name);

		/* Clear the file page's tail where .bss begins mid-page; the
		 * segment must carry write for there to be a tail to clear. */
		if (memsz > filesz && (file_end & (PAGE - 1))) {
			uint64_t page_end = ROUND_UP(file_end);
			uint64_t fault = 0;

			if (s->user_copy_out(s, file_end, zeros,
					     (size_t)(page_end - file_end),
					     &fault) != 0)
				return -1;
		}
	}

	/* Whole .bss pages beyond the file portion, and the pure-.bss case where
	 * there was no file page at all: anonymous, zero on first touch. */
	{
		uint64_t have = filesz > 0 ? ROUND_UP(va + filesz) : map_start;
		uint64_t want = ROUND_UP(va + memsz);

		if (want > have) {
			void *addr = (void *)(uintptr_t)have;

			if (s->as_map(s, &addr, (size_t)(want - have), &anon, 0,
				      prot) != 0)
				return -1;
			vma_record((uint64_t)(uintptr_t)addr, (size_t)(want - have), prot, SUB_BACKING_ANON, 0, "");
		}
	}
	return 0;
}

int elf_load(struct substrate *s, const void *image, size_t len,
	     struct load_info *out)
{
	const struct elf64_ehdr *eh = image;
	const unsigned char *img = image;
	uint64_t bias, lo = UINT64_MAX, hi = 0;
	int i, mapped = 0;

	if (len < sizeof *eh)
		return -1;
	if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
	    eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
		return -1;
	if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB)
		return -1;
	if (eh->e_machine != EM_X86_64)
		return -1;
	if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
		return -1;
	if (eh->e_phoff == 0 || eh->e_phentsize < sizeof(struct elf64_phdr))
		return -1;
	if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len)
		return -1;

	bias = eh->e_type == ET_DYN ? DYN_BASE : 0;

	for (i = 0; i < eh->e_phnum; i++) {
		const struct elf64_phdr *ph = (const struct elf64_phdr *)
			(img + eh->e_phoff + (size_t)i * eh->e_phentsize);
		uint64_t va, end;

		if (ph->p_type != PT_LOAD)
			continue;
		if (ph->p_offset + ph->p_filesz > len)
			return -1;
		if (map_load(s, image, len, ph, bias) != 0)
			return -1;

		va = ROUND_DN(ph->p_vaddr + bias);
		end = ROUND_UP(ph->p_vaddr + bias + ph->p_memsz);
		if (va < lo)
			lo = va;
		if (end > hi)
			hi = end;
		mapped++;
	}
	if (!mapped)
		return -1;

	out->entry = eh->e_entry + bias;
	out->phent = eh->e_phentsize;
	out->phnum = eh->e_phnum;
	out->load_base = lo;
	out->load_end = hi;

	/* AT_PHDR wants the headers' address in the image the program sees.  The
	 * headers sit at e_phoff in the file; find the PT_LOAD whose file range
	 * covers them and translate the offset into that segment's address. */
	out->phdr_va = 0;
	for (i = 0; i < eh->e_phnum; i++) {
		const struct elf64_phdr *ph = (const struct elf64_phdr *)
			(img + eh->e_phoff + (size_t)i * eh->e_phentsize);

		if (ph->p_type != PT_LOAD)
			continue;
		if (eh->e_phoff >= ph->p_offset &&
		    eh->e_phoff < ph->p_offset + ph->p_filesz) {
			out->phdr_va = ph->p_vaddr + bias +
				       (eh->e_phoff - ph->p_offset);
			break;
		}
	}
	return 0;
}
