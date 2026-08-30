/* WP-31: bounds-checked parsing of an ELF64 file image.
 *
 * elf_parse() takes a flat, read-only byte image of a file that claims to be
 * ELF and produces a validated view of its header, program-header table, and
 * dynamic section, with every table reached through them checked to lie
 * wholly inside the image. It reads attacker-shaped input, so it treats every
 * field as hostile: nothing is dereferenced before its span has been proven
 * in-bounds, offset arithmetic cannot overflow past a check, and a structure
 * that is truncated, self-referential, or internally inconsistent is rejected
 * with a diagnostic that names the offending field rather than faulting.
 *
 * The output is a set of file offsets and counts, not pointers into mapped
 * memory. Turning those into a running image is WP-32 (mapping) and beyond;
 * this package only guarantees that what those stages read is structurally
 * sound. The guarantees a caller may rely on are listed in README.md under
 * the heading "Invariants guaranteed to callers", and every field below is
 * covered by one of them.
 */
#ifndef ELFSYSV_LOADER_ELF_PARSE_H
#define ELFSYSV_LOADER_ELF_PARSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rejection codes. elf_ok is the only success value. The set is small on
 * purpose: the human-readable field and message carry the detail, and code
 * exists for a test to assert a class of rejection without matching text. */
typedef enum {
	elf_ok = 0,
	elf_err_size,        /* the image is too small to hold a claimed span */
	elf_err_magic,       /* not ELF, or not the class/data/machine we take */
	elf_err_header,      /* a header field is inconsistent with the format */
	elf_err_phdr,        /* a program header is malformed or out of range */
	elf_err_overlap,     /* two PT_LOAD segments overlap in virtual space */
	elf_err_dynamic,     /* the dynamic section is malformed or truncated */
	elf_err_strtab,      /* the string table is missing, truncated, or bad */
	elf_err_symtab,      /* the symbol table is malformed or out of range */
	elf_err_version,     /* a version-record chain is malformed or loops */
	elf_err_overflow     /* offset arithmetic would exceed 64 bits */
} elf_err;

/* A rejection. field names the ELF member at fault, in a form a reader can
 * find in the specification (for example "e_phoff", "PT_LOAD[2].p_offset",
 * "DT_NEEDED", "Elf64_Verneed.vn_next"). msg restates it in a sentence and
 * includes the offending value. On success code is elf_ok and field is 0. */
typedef struct {
	elf_err     code;
	const char *field;
	char        msg[256];
} elf_diag;

/* One PT_LOAD segment, as file offsets and virtual addresses that have been
 * checked. off + filesz lies within the image; vaddr + memsz does not
 * overflow; filesz <= memsz. */
typedef struct {
	uint64_t off;
	uint64_t vaddr;
	uint64_t filesz;
	uint64_t memsz;
	uint32_t flags;
	uint64_t align;
} elf_load_seg;

#define ELF_MAX_LOAD 64   /* PT_LOAD segments kept; more is rejected */
#define ELF_MAX_NEEDED 256 /* DT_NEEDED offsets kept; more is rejected */

/* The validated view. Every offset is a file offset into the parsed image and
 * every (offset, size) pair has been proven to lie within it. A field guarded
 * by a has_* flag is meaningful only when that flag is set. */
typedef struct {
	/* header */
	uint16_t e_type;
	uint16_t e_machine;
	uint64_t e_entry;
	uint64_t phoff;
	uint16_t phnum;         /* resolved, PN_XNUM already expanded */
	uint16_t phentsize;

	/* program headers */
	elf_load_seg load[ELF_MAX_LOAD];
	unsigned     load_count;

	int      has_interp;
	uint64_t interp_off;
	uint64_t interp_size;

	int      has_tls;
	uint64_t tls_off, tls_filesz, tls_memsz, tls_align;

	int      has_relro;
	uint64_t relro_off, relro_size;

	/* dynamic section (file offsets) */
	int      has_dynamic;
	uint64_t dyn_off;
	uint64_t dyn_count;     /* entries up to and excluding DT_NULL */

	/* string table */
	int      has_strtab;
	uint64_t strtab_off;
	uint64_t strsz;

	/* symbol table (count is not knowable at this layer; start is checked) */
	int      has_symtab;
	uint64_t symtab_off;
	uint64_t syment;

	/* .gnu.version, one Elf64_Versym per symbol; start is checked */
	int      has_versym;
	uint64_t versym_off;

	/* version definitions and needs, fully walked and bounds-checked */
	int      has_verdef;
	uint64_t verdef_off;
	uint64_t verdefnum;
	int      has_verneed;
	uint64_t verneed_off;
	uint64_t verneednum;

	/* names, as validated offsets into the string table */
	int      has_soname;
	uint64_t soname;                     /* strtab-relative */
	uint64_t needed[ELF_MAX_NEEDED];     /* strtab-relative */
	unsigned needed_count;
} elf_parsed;

/* Parse and validate. Returns elf_ok on success, having filled out; returns a
 * nonzero elf_err on rejection, having filled diag with the field and a
 * message. out is left partially filled on rejection and must not be read.
 * Neither pointer may be null; image may be null only if size is zero, which
 * is itself rejected. The function never reads outside [image, image+size)
 * and never modifies the image. */
elf_err elf_parse(const unsigned char *image, size_t size,
                  elf_parsed *out, elf_diag *diag);

/* A stable, human-readable name for a code, for test output. */
const char *elf_err_name(elf_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_ELF_PARSE_H */
