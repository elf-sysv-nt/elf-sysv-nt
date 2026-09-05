/*
 * vma.h -- the core's record of what is mapped where.
 *
 * The substrate keeps no map of its own; as_map's contract says the range is
 * "the core's to record".  This is that record.  Phase 1 maps three things --
 * the program's segments, the stack, the vDSO -- and the tree exists because
 * /proc/self/maps has to be written from something, and because every later
 * phase (fork, mprotect, the fault handler) reads the same structure rather
 * than asking the host what it thinks is mapped.
 *
 * Ranges are kept sorted by start address and never overlap.  Adjacent ranges
 * are not coalesced: a program's text and data segments differ in protection
 * and a reader of the map wants to see both.
 */
#ifndef CORE_VMA_H
#define CORE_VMA_H

#include <stddef.h>
#include <stdint.h>

#include "substrate.h"

#define VMA_MAX		64
#define VMA_NAME_MAX	64

struct vma {
	uint64_t start;			/* inclusive */
	uint64_t end;			/* exclusive */
	int prot;			/* the Linux PROT_* set */
	enum sub_backing_kind kind;
	uint64_t offset;		/* file offset for a file backing */
	char name[VMA_NAME_MAX];	/* "[stack]", "[vdso]", or a path */
};

/* Forget every range.  Called once before a process image is built. */
void vma_reset(void);

/*
 * Record a mapped range.  start and len are the values handed to as_map, so a
 * caller records what it actually got rather than what it asked for.  Returns 0,
 * or -1 if the tree is full or the range would overlap one already recorded --
 * both of which are core bugs rather than user errors, and neither is silent.
 */
int vma_record(uint64_t start, size_t len, int prot,
	       enum sub_backing_kind kind, uint64_t offset, const char *name);

/* How many ranges are recorded, and the i'th in address order. */
size_t vma_count(void);
const struct vma *vma_at(size_t i);

/*
 * Write the ranges in /proc/self/maps format into buf, NUL-terminated, and
 * return the byte count written excluding the terminator.  A buffer too small
 * truncates at a line boundary rather than mid-line, so a short read never
 * shows a caller half an address.
 */
size_t vma_render_maps(char *buf, size_t cap);

#endif /* CORE_VMA_H */
