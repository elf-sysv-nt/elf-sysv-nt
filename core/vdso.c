/*
 * vdso.c -- build the vDSO image and map it.
 *
 * The image is assembled here rather than linked as a separate object because
 * it is one page with no code in it yet, and a build step that produces an
 * empty shared library is more machinery than the thing it produces.  When the
 * first real entry point arrives -- clock_gettime, once there is a clock --
 * this becomes a linked object with its own source, and the mapping code below
 * does not change.
 *
 * What a reader of AT_SYSINFO_EHDR must find is an ELF64 header it can trust,
 * a PT_LOAD covering the image, and a PT_DYNAMIC leading to a DT_SONAME.  glibc
 * checks the header, walks the phdrs, and gives up quietly on an object whose
 * symbol table has nothing it wants, which is the behaviour this relies on.
 */
#include <string.h>

#include "vdso.h"
#include "vma.h"

#define VDSO_SIZE	4096u

/* Minimal ELF constants, from the target's elf.h rather than the host's. */
#define ET_DYN		3
#define EM_X86_64	62
#define EV_CURRENT	1
#define PT_LOAD		1
#define PT_DYNAMIC	2
#define PF_X		1
#define PF_R		4
#define DT_NULL		0
#define DT_STRTAB	5
#define DT_SONAME	14

struct e64hdr {
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct p64hdr {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
};

struct d64 {
	int64_t d_tag;
	uint64_t d_val;
};

/*
 * Lay the image out at fixed offsets.  Keeping them constants rather than
 * computing them keeps the arithmetic auditable against a hexdump, which is how
 * anyone will actually debug a malformed vDSO.
 */
#define OFF_PHDR	64
#define OFF_DYN		256
#define OFF_STR		384

static void build(unsigned char *img)
{
	static const char soname[] = "linux-vdso.so.1";
	struct e64hdr eh;
	struct p64hdr ph[2];
	struct d64 dyn[3];

	memset(img, 0, VDSO_SIZE);

	memset(&eh, 0, sizeof eh);
	eh.e_ident[0] = 0x7f; eh.e_ident[1] = 'E';
	eh.e_ident[2] = 'L';  eh.e_ident[3] = 'F';
	eh.e_ident[4] = 2;		/* ELFCLASS64 */
	eh.e_ident[5] = 1;		/* ELFDATA2LSB */
	eh.e_ident[6] = EV_CURRENT;
	eh.e_type = ET_DYN;
	eh.e_machine = EM_X86_64;
	eh.e_version = EV_CURRENT;
	eh.e_phoff = OFF_PHDR;
	eh.e_ehsize = sizeof eh;
	eh.e_phentsize = sizeof ph[0];
	eh.e_phnum = 2;
	memcpy(img, &eh, sizeof eh);

	memset(ph, 0, sizeof ph);
	ph[0].p_type = PT_LOAD;
	ph[0].p_flags = PF_R | PF_X;
	ph[0].p_filesz = ph[0].p_memsz = VDSO_SIZE;
	ph[0].p_align = VDSO_SIZE;
	ph[1].p_type = PT_DYNAMIC;
	ph[1].p_flags = PF_R;
	ph[1].p_offset = ph[1].p_vaddr = OFF_DYN;
	ph[1].p_filesz = ph[1].p_memsz = sizeof dyn;
	ph[1].p_align = 8;
	memcpy(img + OFF_PHDR, ph, sizeof ph);

	dyn[0].d_tag = DT_STRTAB; dyn[0].d_val = OFF_STR;
	dyn[1].d_tag = DT_SONAME; dyn[1].d_val = 1;	/* index 0 is the empty string */
	dyn[2].d_tag = DT_NULL;   dyn[2].d_val = 0;
	memcpy(img + OFF_DYN, dyn, sizeof dyn);

	img[OFF_STR] = '\0';
	memcpy(img + OFF_STR + 1, soname, sizeof soname);
}

int vdso_map(struct substrate *s, uint64_t *base)
{
	struct sub_backing anon = { SUB_BACKING_VDSO, NULL, 0 };
	unsigned char img[VDSO_SIZE];
	void *ubase = NULL;
	uint64_t fault = 0;

	/* Map it writable, lay the image down through the user-copy path, then
	 * drop write.  The core never dereferences a user address (invariant 2),
	 * so even its own page is written the same way a program's would be. */
	if (s->as_map(s, &ubase, VDSO_SIZE, &anon, 0,
		      SUB_PROT_READ | SUB_PROT_WRITE) != 0)
		return -1;

	build(img);
	if (s->user_copy_out(s, (uint64_t)(uintptr_t)ubase, img, VDSO_SIZE,
			     &fault) != 0) {
		s->as_unmap(s, ubase, VDSO_SIZE);
		return -1;
	}
	if (s->as_protect(s, ubase, VDSO_SIZE,
			  SUB_PROT_READ | SUB_PROT_EXEC) != 0) {
		s->as_unmap(s, ubase, VDSO_SIZE);
		return -1;
	}

	if (vma_record((uint64_t)(uintptr_t)ubase, VDSO_SIZE,
		       SUB_PROT_READ | SUB_PROT_EXEC, SUB_BACKING_VDSO, 0,
		       "[vdso]") != 0) {
		s->as_unmap(s, ubase, VDSO_SIZE);
		return -1;
	}

	*base = (uint64_t)(uintptr_t)ubase;
	return 0;
}
