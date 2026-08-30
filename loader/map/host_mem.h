/* WP-32: the two host memory granularities the mapper needs.
 *
 * ELF aligns segments at the page size; Windows reserves address space at a
 * coarser allocation granularity, and on this host it is also the granularity
 * at which a protection change can be made. The mapper reads both from the
 * host rather than assuming a value, so the same code is correct on a host
 * whose granularity differs. These are the only two facts the mapper takes
 * from the Win32 side, kept in their own translation unit so <windows.h> does
 * not have to be included beside the POSIX memory headers.
 */
#ifndef ELFSYSV_LOADER_MAP_HOST_MEM_H
#define ELFSYSV_LOADER_MAP_HOST_MEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The page size: the finest granularity at which the format aligns and at
 * which committed memory is tracked. 0x1000 on x86-64. */
uint64_t elf_map_host_page_size(void);

/* The allocation granularity: the coarsest boundary the host places a
 * reservation on, and on the pinned host also the boundary a protection change
 * snaps to. 0x10000 on Windows. */
uint64_t elf_map_host_granule(void);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_MAP_HOST_MEM_H */
