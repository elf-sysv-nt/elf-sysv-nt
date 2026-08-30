/* WP-32: read the host page size and allocation granularity from Win32.
 *
 * Isolated here so the rest of the mapper compiles against the POSIX memory
 * interface alone. GetSystemInfo is the source of truth for both values; the
 * Cygwin runtime passes it through to the kernel unchanged, so it is valid
 * whether this runs under a bare stub or under the re-faced runtime later. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "host_mem.h"

uint64_t elf_map_host_page_size(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return (uint64_t) si.dwPageSize;
}

uint64_t elf_map_host_granule(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return (uint64_t) si.dwAllocationGranularity;
}
