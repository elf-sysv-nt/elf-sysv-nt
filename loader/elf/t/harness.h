/* Guard-page image loader, shared by the unit and fuzz drivers.
 *
 * This host toolchain has no AddressSanitizer runtime, so the memory-safety
 * net is built from page protection instead. An image of n bytes is copied so
 * that its last byte abuts an unmapped guard page: any read at image[n] or
 * beyond faults immediately and deterministically rather than returning
 * adjacent heap bytes. A parser that respects its (base, size) bound never
 * touches the guard; one that miscomputes a stride walks straight into it and
 * the run dies with a SIGSEGV the fuzz loop reports as a crash.
 *
 * This catches exactly the read-past-the-end class that a truncated ELF is
 * built to provoke, which is the class WP-31 exists to make safe.
 */
#ifndef ELFSYSV_LOADER_ELF_HARNESS_H
#define ELFSYSV_LOADER_ELF_HARNESS_H

#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
	unsigned char *base;   /* image start; base[len] is on the guard page */
	size_t         len;
	void          *map;    /* whole mapping, for guard_free */
	size_t         maplen;
} guard_buf;

/* Copy n bytes of data into a fresh guarded buffer. Returns 0 on success,
 * -1 if the mapping could not be made. n may be zero. */
static int guard_load(const unsigned char *data, size_t n, guard_buf *g)
{
	long pg = sysconf(_SC_PAGESIZE);
	size_t page = (pg > 0) ? (size_t)pg : 4096;
	size_t datapages = (n + page - 1) / page;
	size_t maplen;
	unsigned char *m, *guard, *p;

	if (datapages == 0)
		datapages = 1;
	maplen = (datapages + 1) * page;
	m = (unsigned char *)mmap(NULL, maplen, PROT_READ | PROT_WRITE,
	                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
		return -1;
	guard = m + datapages * page;
	if (mprotect(guard, page, PROT_NONE) != 0) {
		munmap(m, maplen);
		return -1;
	}
	p = guard - n;
	if (n)
		memcpy(p, data, n);
	g->base = p;
	g->len = n;
	g->map = m;
	g->maplen = maplen;
	return 0;
}

static void guard_free(guard_buf *g)
{
	if (g->map)
		munmap(g->map, g->maplen);
	g->map = 0;
}

#endif /* ELFSYSV_LOADER_ELF_HARNESS_H */
