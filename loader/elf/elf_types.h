/* ELF64 structural definitions, self-contained.
 *
 * The parser reads a file image byte by byte and never dereferences one of
 * these structures over untrusted memory, so what matters here is the numeric
 * layout: the field offsets and widths the on-disk format fixes, and the
 * constant values the parser tests against. The structs are carried for
 * documentation and for the one place a validated, in-bounds copy is read out
 * with memcpy; nothing casts a pointer into the image onto them.
 *
 * Values follow the System V AMD64 ABI and the generic ELF specification.
 * Only the little-endian ELFCLASS64 shape this project targets is described;
 * a 32-bit or big-endian image is rejected at the header rather than parsed.
 *
 * This header deliberately does not include the host <elf.h>. The parser is
 * meant to run later under the runtime, where the host's headers are not the
 * authority on the format it reads, so it carries its own.
 */
#ifndef ELFSYSV_LOADER_ELF_TYPES_H
#define ELFSYSV_LOADER_ELF_TYPES_H

#include <stdint.h>

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Section;
typedef uint16_t Elf64_Versym;

#define EI_NIDENT 16

typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half    e_type;
	Elf64_Half    e_machine;
	Elf64_Word    e_version;
	Elf64_Addr    e_entry;
	Elf64_Off     e_phoff;
	Elf64_Off     e_shoff;
	Elf64_Word    e_flags;
	Elf64_Half    e_ehsize;
	Elf64_Half    e_phentsize;
	Elf64_Half    e_phnum;
	Elf64_Half    e_shentsize;
	Elf64_Half    e_shnum;
	Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	Elf64_Word  p_type;
	Elf64_Word  p_flags;
	Elf64_Off   p_offset;
	Elf64_Addr  p_vaddr;
	Elf64_Addr  p_paddr;
	Elf64_Xword p_filesz;
	Elf64_Xword p_memsz;
	Elf64_Xword p_align;
} Elf64_Phdr;

typedef struct {
	Elf64_Word    sh_name;
	Elf64_Word    sh_type;
	Elf64_Xword   sh_flags;
	Elf64_Addr    sh_addr;
	Elf64_Off     sh_offset;
	Elf64_Xword   sh_size;
	Elf64_Word    sh_link;
	Elf64_Word    sh_info;
	Elf64_Xword   sh_addralign;
	Elf64_Xword   sh_entsize;
} Elf64_Shdr;

typedef struct {
	Elf64_Sxword d_tag;
	union {
		Elf64_Xword d_val;
		Elf64_Addr  d_ptr;
	} d_un;
} Elf64_Dyn;

typedef struct {
	Elf64_Word    st_name;
	unsigned char st_info;
	unsigned char st_other;
	Elf64_Section st_shndx;
	Elf64_Addr    st_value;
	Elf64_Xword   st_size;
} Elf64_Sym;

/* Symbol-versioning records. */
typedef struct {
	Elf64_Half vd_version;
	Elf64_Half vd_flags;
	Elf64_Half vd_ndx;
	Elf64_Half vd_cnt;
	Elf64_Word vd_hash;
	Elf64_Word vd_aux;
	Elf64_Word vd_next;
} Elf64_Verdef;

typedef struct {
	Elf64_Word vda_name;
	Elf64_Word vda_next;
} Elf64_Verdaux;

typedef struct {
	Elf64_Half vn_version;
	Elf64_Half vn_cnt;
	Elf64_Word vn_file;
	Elf64_Word vn_aux;
	Elf64_Word vn_next;
} Elf64_Verneed;

typedef struct {
	Elf64_Word vna_hash;
	Elf64_Half vna_flags;
	Elf64_Half vna_other;
	Elf64_Word vna_name;
	Elf64_Word vna_next;
} Elf64_Vernaux;

/* Sizes the format fixes.  The parser tests declared entry sizes against
 * these rather than against sizeof over a padded host struct. */
#define ELF64_EHDR_SIZE     64
#define ELF64_PHDR_SIZE     56
#define ELF64_SHDR_SIZE     64
#define ELF64_DYN_SIZE      16
#define ELF64_SYM_SIZE      24
#define ELF64_VERDEF_SIZE   20
#define ELF64_VERDAUX_SIZE  8
#define ELF64_VERNEED_SIZE  16
#define ELF64_VERNAUX_SIZE  16
#define ELF64_VERSYM_SIZE   2

/* e_ident */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_ABIVERSION 8

#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

#define ELFDATANONE  0
#define ELFDATA2LSB  1
#define ELFDATA2MSB  2

#define EV_NONE      0
#define EV_CURRENT   1

/* e_type */
#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3
#define ET_CORE 4

/* e_machine */
#define EM_X86_64 62

/* Sentinel program-header count: the real count lives in shdr[0].sh_info. */
#define PN_XNUM 0xffff

/* p_type */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552
#define PT_GNU_PROPERTY 0x6474e553

/* d_tag */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH      29
#define DT_FLAGS        30
#define DT_GNU_HASH     0x6ffffef5
#define DT_RELACOUNT    0x6ffffff9
#define DT_RELCOUNT     0x6ffffffa
#define DT_FLAGS_1      0x6ffffffb
#define DT_VERSYM       0x6ffffff0
#define DT_VERDEF       0x6ffffffc
#define DT_VERDEFNUM    0x6ffffffd
#define DT_VERNEED      0x6ffffffe
#define DT_VERNEEDNUM   0x6fffffff

#endif /* ELFSYSV_LOADER_ELF_TYPES_H */
