/* WP-32: placing a validated ELF object in memory.
 *
 * elf_map() takes the validated view WP-31 produced (an elf_parsed) together
 * with the flat file image it was parsed from, and turns the PT_LOAD set into
 * a running image: one reserved-and-committed region per object, each segment
 * copied to its runtime address, .bss arriving zeroed for free, and every
 * segment carrying its final protection after a second pass that runs only
 * once every copy is done. The placement is made through the host C library's
 * mmap and mprotect, so the runtime's own memory bookkeeping records it and a
 * later fork can replay it; that visibility is the property WP-41 depends on.
 *
 * What this package does not do is relocate or resolve a symbol; those are
 * WP-34 and beyond. Because PT_GNU_RELRO must be frozen read-only only after
 * relocation has written through it, elf_map() records the range but does not
 * freeze it. elf_map_protect_relro() is the hook the relocation stage calls
 * when the writes are done; for an object that carries no relocations it may
 * be called immediately.
 *
 * The mapping arithmetic and the findings it rests on were settled by
 * spike/map-and-jump (spike 2); the host-granule constraint that shapes the
 * protection pass is recorded in doc/decisions/0008-mmap-granule-protection.md.
 */
#ifndef ELFSYSV_LOADER_MAP_H
#define ELFSYSV_LOADER_MAP_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rejection and failure codes. elf_map_ok is the only success value. A code
 * in the err_host family means the host refused an operation the loader asked
 * for; a code in the err_image family means the object cannot be honored on
 * this host. err_bss is neither: it means a property this package is built to
 * rely on -- that a freshly committed page is zero -- did not hold, which is a
 * broken assumption about the host rather than a bad object, and is fatal. */
typedef enum {
	elf_map_ok = 0,
	elf_map_err_arg,      /* a precondition on the arguments was violated */
	elf_map_err_span,     /* the object's virtual span is empty or overflows */
	elf_map_err_granule,  /* two segments of unlike protection share a host
	                       * allocation granule and cannot be separated */
	elf_map_err_reserve,  /* the host refused the reservation at the base */
	elf_map_err_commit,   /* a segment would not commit */
	elf_map_err_protect,  /* a segment would not take its protection */
	elf_map_err_bss       /* a freshly committed page was not zero */
} elf_map_err;

typedef struct {
	elf_map_err code;
	const char *field;    /* the ELF member or host operation at fault */
	char        msg[256];
} elf_map_diag;

/* One PT_LOAD after placement. vaddr is the runtime address (link vaddr plus
 * load_bias); prot_lo..prot_hi is the page-rounded range that actually took
 * the protection, which is what a fault probe should test. */
typedef struct {
	uint64_t vaddr;
	uint64_t filesz;
	uint64_t memsz;
	uint32_t flags;       /* PF_R | PF_W | PF_X, from the program header */
	uint64_t prot_lo;
	uint64_t prot_hi;
} elf_map_seg;

/* The placed object. base..base+size is the single reservation. Every runtime
 * address in the image is a link address plus load_bias, which is zero for an
 * ET_EXEC honored at its link addresses and nonzero for an ET_DYN placed at a
 * chosen base. */
typedef struct {
	uint64_t base;
	uint64_t size;
	uint64_t load_bias;
	uint64_t entry;       /* e_entry + load_bias */

	unsigned    seg_count;
	elf_map_seg seg[ELF_MAX_LOAD];

	/* PT_GNU_RELRO, translated to a runtime page range. Recorded but not
	 * frozen until elf_map_protect_relro runs; see the header comment. */
	int      has_relro;
	uint64_t relro_lo;
	uint64_t relro_hi;
	int      relro_applied;

	/* PT_GNU_STACK, recorded for WP-40/WP-41. The stack itself is theirs;
	 * this only carries whether the object asked for an executable one. */
	int has_gnu_stack;
	int stack_exec;

	/* The host granularity in force when the object was placed. */
	uint64_t page_size;
	uint64_t granule;
} elf_mapping;

/* Place the object. image[0..image_size) is the file p was parsed from; p must
 * be the elf_ok result of elf_parse over exactly that image. base_hint chooses
 * the load base for an ET_DYN and is ignored for an ET_EXEC. On success out is
 * filled and diag is left untouched; on failure a nonzero code is returned,
 * diag names the field or operation at fault, and any partial reservation has
 * been released. Neither image, p, out, nor diag may be null. */
elf_map_err elf_map(const unsigned char *image, size_t image_size,
                    const elf_parsed *p, uint64_t base_hint,
                    elf_mapping *out, elf_map_diag *diag);

/* Freeze the recorded PT_GNU_RELRO range read-only. A no-op that returns
 * elf_map_ok when the object carries no relro or the range is already frozen,
 * so a caller may call it unconditionally. This is the hook the relocation
 * stage (WP-34) calls once it has finished writing through the range. */
elf_map_err elf_map_protect_relro(elf_mapping *m, elf_map_diag *diag);

/* Release the whole reservation and reset m. Safe on a zeroed or already
 * released mapping. */
void elf_unmap(elf_mapping *m);

/* A stable, human-readable name for a code, for test output. */
const char *elf_map_err_name(elf_map_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_MAP_H */
