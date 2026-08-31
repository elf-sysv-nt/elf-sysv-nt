/* The Win32 half of the overlap characterization, kept apart so <windows.h>
 * does not sit beside the POSIX memory headers — the same split loader/map
 * keeps between elf_map.c and host_mem.c.
 *
 * Two probes. One asks whether VirtualAlloc(MEM_RESERVE) still refuses a
 * reservation over a span the POSIX layer already committed, which localizes
 * the divergence: if the POSIX mmap now overlays where the Win32 reserve still
 * refuses, the change is in Cygwin's mmap conformance rather than in the host.
 * The other records where a plain reserve lands, for the transcript's context.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* 1 refused, 0 allowed, -1 probe error. On a Windows reservation the address
 * must be granule-aligned; the caller passes an mmap'd base, which is. */
int win_reserve_over_occupied(void *addr, size_t len)
{
	void *got = VirtualAlloc(addr, len, MEM_RESERVE, PAGE_NOACCESS);
	if (got == NULL)
		return 1;                 /* refused, as a reserve over occupied should be */
	/* It somehow took. Undo it so the probe leaves nothing behind. Only
	 * release if Windows handed us our own fresh reservation rather than a
	 * view into the existing one; MEM_RELEASE on the whole base is safe here
	 * because a MEM_RESERVE return is always a new region base. */
	VirtualFree(got, 0, MEM_RELEASE);
	return 0;
}

int win_reserve_free(uint64_t *base_out, uint64_t *size_out)
{
	SIZE_T len = 0x80000;         /* eight granules, same as the probe span */
	void *got = VirtualAlloc(NULL, len, MEM_RESERVE, PAGE_NOACCESS);
	if (got == NULL) {
		*base_out = 0;
		*size_out = 0;
		return -1;
	}
	*base_out = (uint64_t)(uintptr_t) got;
	*size_out = (uint64_t) len;
	VirtualFree(got, 0, MEM_RELEASE);
	return 0;
}
