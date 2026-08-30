/* WP-34: applying relocations to a mapped object graph.
 *
 * WP-31 validated an object, WP-32 placed it in memory, and WP-33 walked the
 * graph of objects a program needs. This package is what turns that placed
 * graph into one whose code and data actually point at each other: it reads
 * each object's relocation tables out of its own mapped image and writes the
 * computed values through that image, so a call through the PLT reaches the
 * body that defines it and a data reference reaches the datum.
 *
 * It carries the relocation types el8 objects actually contain -- measured,
 * not guessed, against the pinned Rocky 8.10 glibc and its companions:
 * R_X86_64_RELATIVE, _GLOB_DAT, _JUMP_SLOT, _64, _COPY, _IRELATIVE, and the
 * static-TLS trio _TPOFF64/_DTPMOD64/_DTPOFF64 -- plus RELR, which el8 does not
 * emit but a newer toolchain does, and both binding disciplines: BIND_NOW,
 * which resolves every PLT slot at load, and lazy binding, which leaves the
 * slot pointing back into the PLT until the first call trips the resolver.
 *
 * Symbol resolution here is the minimum a relocation needs: a first-definition
 * scan of the scope in load order, with STT_GNU_IFUNC resolvers run to their
 * chosen body. The hashed lookup, the scope and interposition rules, and
 * symbol versioning are WP-35 and WP-36; this package resolves what it must to
 * relocate and no more, and README.md draws that line.
 */
#ifndef ELFSYSV_LOADER_RELOC_H
#define ELFSYSV_LOADER_RELOC_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_types.h"
#include "../elf/elf_parse.h"
#include "../map/elf_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* x86-64 relocation types this engine computes. The numbers are the psABI's;
 * they are named here rather than pulled from a host <elf.h> for the same
 * reason WP-31 carries its own structures. */
#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_PC32       2
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_DTPMOD64   16
#define R_X86_64_DTPOFF64   17
#define R_X86_64_TPOFF64    18
#define R_X86_64_COPY       5
#define R_X86_64_IRELATIVE  37

/* Field accessors over Elf64_Rela.r_info. */
#define ELF64_R_SYM(i)   ((uint32_t) ((i) >> 32))
#define ELF64_R_TYPE(i)  ((uint32_t) ((i) & 0xffffffffu))

/* st_info accessors and the symbol constants the resolver tests. */
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STT_FUNC       2
#define STT_GNU_IFUNC  10
#define SHN_UNDEF   0

/* Extra dynamic tags used only here (RELR and the relocation-count hints are
 * outside the set WP-31 needed). Values are the generic ABI's. */
#define DT_RELR      36
#define DT_RELRSZ    35
#define DT_RELRENT   37
#define DF_BIND_NOW  0x8
#define DF_1_NOW     0x1

/* Rela record, matching the on-disk layout the parser proved in-bounds. */
typedef struct {
	Elf64_Addr  r_offset;
	Elf64_Xword r_info;
	Elf64_Sxword r_addend;
} Elf64_Rela;

/* Outcome codes. elf_reloc_ok is the only success. */
typedef enum {
	elf_reloc_ok = 0,
	elf_reloc_err_arg,          /* a precondition on the arguments failed */
	elf_reloc_err_scope_full,   /* more objects than the scope can hold */
	elf_reloc_err_dynamic,      /* the dynamic section is missing or bad */
	elf_reloc_err_symtab,       /* symbol count undiscoverable or out of range */
	elf_reloc_err_unsupported,  /* a relocation type this engine does not carry */
	elf_reloc_err_undef,        /* a needed, non-weak symbol was not found */
	elf_reloc_err_tls,          /* a TLS relocation with no static layout to use */
	elf_reloc_err_relro         /* freezing PT_GNU_RELRO failed */
} elf_reloc_err;

typedef struct {
	elf_reloc_err code;
	const char   *field;  /* the ELF member or object at fault */
	char          msg[256];
} elf_reloc_diag;

#define ELF_RELOC_MAX_OBJ 64

struct elf_reloc_scope;

/* One object in the scope, with the dynamic view this engine reads out of its
 * mapped image once, in elf_reloc_add. Every pointer here is a runtime address
 * inside the mapping (link address plus load bias), already translated. The
 * struct is public because a stable pointer to it is the cookie the lazy PLT
 * carries in GOT[1], and the fixup and the tests read it back. */
typedef struct elf_reloc_object {
	elf_mapping       *map;
	const elf_parsed  *parsed;
	const char        *name;      /* soname or path, for diagnostics */
	uint64_t           bias;      /* map->load_bias */

	const char       *strtab;
	uint64_t          strsz;
	const Elf64_Sym  *symtab;
	uint64_t          symcount;   /* discovered from .hash or .gnu.hash */
	const uint32_t   *sysv_hash;  /* .hash, or NULL */
	const uint32_t   *gnu_hash;   /* .gnu.hash, or NULL */

	const Elf64_Rela *rela;       /* .rela.dyn */
	uint64_t          rela_n;
	const Elf64_Rela *jmprel;     /* .rela.plt */
	uint64_t          jmprel_n;
	const uint64_t   *relr;       /* .relr.dyn, or NULL */
	uint64_t          relr_n;     /* entries */
	uint64_t         *pltgot;     /* .got.plt, or NULL */

	int      bind_now;            /* DF_BIND_NOW | DF_1_NOW | DT_BIND_NOW seen */

	int      has_tls;             /* the object carries PT_TLS */
	uint64_t tls_modid;           /* 1-based module id in the scope */
	int64_t  tls_tpoff;           /* offset of this module's block from the tp */

	struct elf_reloc_scope *scope;  /* back-pointer, for the lazy fixup */
} elf_reloc_object;

/* The scope: the objects to relocate, in load order. obj[0] is the root, and
 * that order is the resolution order a first-definition search walks. */
typedef struct elf_reloc_scope {
	elf_reloc_object obj[ELF_RELOC_MAX_OBJ];
	unsigned         count;
	uint64_t         tls_static_size;  /* running total as modules are added */
} elf_reloc_scope;

/* Reset a scope to empty. */
void elf_reloc_scope_init(elf_reloc_scope *s);

/* Record one placed object and read its dynamic view. m and p must describe the
 * same object (p the elf_ok parse, m the elf_map result over it). name is
 * borrowed, not copied. Objects must be added in load order, root first. On a
 * malformed or missing dynamic section a nonzero code is returned and diag is
 * filled; on success the object is appended and its tables are ready to apply. */
elf_reloc_err elf_reloc_add(elf_reloc_scope *s, elf_mapping *m,
                            const elf_parsed *p, const char *name,
                            elf_reloc_diag *diag);

/* Apply every object's relocations. Assigns static-TLS module ids and offsets
 * across the scope, applies RELR and RELA, resolves cross-object symbols in
 * load order, runs IRELATIVE resolvers, sets up either eager or lazy PLT
 * binding per object, and freezes each object's PT_GNU_RELRO. Returns
 * elf_reloc_ok with the graph fully wired, or a nonzero code with diag naming
 * the object and field at fault. */
elf_reloc_err elf_reloc_apply(elf_reloc_scope *s, elf_reloc_diag *diag);

/* Apply only the relocations an object can satisfy against itself, without a
 * resolved scope and without running any code: RELATIVE, RELR, and the static
 * TLS trio TPOFF64/DTPMOD64/DTPOFF64. This is the bootstrap subset a loader
 * relocates first (glibc's ELF_DYNAMIC_RELOCATE does relative before the rest),
 * and it is how TPOFF64 is certified over a real vendor object here, since the
 * platform's own toolchain refuses to emit a %fs TLS relocation from source.
 * Symbol-resolving and PLT relocations are left untouched. */
elf_reloc_err elf_reloc_apply_bootstrap(elf_reloc_scope *s, elf_reloc_diag *diag);

/* Apply a RELR relative-relocation stream: n entries at relr, each an address
 * (even) or a 63-word bitmap (odd), relocating each named word by adding bias.
 * This is the whole of RELR support, factored out because neither this
 * platform's toolchain nor el8 emits RELR, so it is certified directly over a
 * constructed stream rather than a linked object. Also called internally when
 * an object does carry DT_RELR. */
void elf_reloc_relr(uint64_t bias, const uint64_t *relr, uint64_t n);

/* The lazy-binding fixup. The PLT resolver trampoline calls this with the
 * cookie it found in GOT[1] and the relocation index the PLT pushed; it
 * resolves that one JUMP_SLOT, writes the GOT, and returns the target address
 * for the trampoline to jump to. Exposed for the trampoline and the tests. */
uint64_t elf_reloc_fixup(elf_reloc_object *o, uint64_t rela_index);

/* A stable, human-readable name for a code, for test output. */
const char *elf_reloc_err_name(elf_reloc_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_RELOC_H */
