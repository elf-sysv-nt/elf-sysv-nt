/* elfcore.h -- write an ELF core the WP-60 gdb reads.
 *
 * WP-61, under DR-0033. The writer takes the register file the delivery
 * path captured (signal.h's elfsysv_sigctx_t), a description of the dying
 * process, and a list of memory segments, and emits an ET_CORE image
 * through a caller-supplied sink. It opens no file and walks no memory:
 * collection is the caller's side of the seam, so a test can drive the
 * writer without a crashing process in the loop.
 */
#ifndef ELFSYSV_ELFCORE_H
#define ELFSYSV_ELFCORE_H

#include <stddef.h>
#include <stdint.h>

#include "../signal/signal.h"

/* One memory segment to be carried as a PT_LOAD. When path is non-NULL the
 * segment also appears in the NT_FILE note, which is how gdb rebuilds the
 * shared-library list from a core without the crashed process around. */
typedef struct {
	uint64_t vaddr;
	uint64_t len;
	const void *bytes;	/* len bytes, read at write time */
	unsigned prot;		/* ELFCORE_R | ELFCORE_W | ELFCORE_X */
	const char *path;	/* backing object, or NULL if anonymous */
	uint64_t file_off;	/* offset within path, pages (NT_FILE) */
} elfcore_seg_t;

#define ELFCORE_R 4u
#define ELFCORE_W 2u
#define ELFCORE_X 1u

/* The process the core describes. auxv may be NULL; when present it is the
 * raw auxiliary vector, AT_NULL terminator included, copied verbatim into
 * an NT_AUXV note. */
typedef struct {
	int signo;		/* the fatal signal */
	int pid;
	const char *fname;	/* command name, 15 bytes kept */
	const char *psargs;	/* argument string, 79 bytes kept */
	const elfsysv_sigctx_t *ctx;
	const void *auxv;
	size_t auxv_len;
} elfcore_proc_t;

/* The sink. Returns n on success; anything else aborts the write and is
 * returned to the caller of elfcore_write. */
typedef long (*elfcore_sink_t)(void *cookie, const void *buf, size_t n);

/* Emit the core. Returns 0 on success, -1 on a short or failed sink write,
 * -2 on an argument it refuses (no ctx, no segments, absurd counts). */
int elfcore_write(elfcore_sink_t sink, void *cookie,
		  const elfcore_proc_t *proc,
		  const elfcore_seg_t *segs, size_t nsegs);

#endif /* ELFSYSV_ELFCORE_H */
