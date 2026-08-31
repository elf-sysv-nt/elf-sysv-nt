/* WP-27: the Win32 bodies for shim/sys/mman.h. See that header for why this
 * exists at all. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>

#include "sys/mman.h"

static DWORD page_of(int prot)
{
	switch (prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) {
	case PROT_NONE:              return PAGE_NOACCESS;
	case PROT_READ:              return PAGE_READONLY;
	case PROT_WRITE:             /* Windows has no write-only page */
	case PROT_READ | PROT_WRITE: return PAGE_READWRITE;
	case PROT_EXEC:              return PAGE_EXECUTE;
	case PROT_READ | PROT_EXEC:  return PAGE_EXECUTE_READ;
	default:                     return PAGE_EXECUTE_READWRITE;
	}
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off)
{
	void *got;
	(void) flags; (void) fd; (void) off;
	got = VirtualAlloc(addr, len, MEM_RESERVE | MEM_COMMIT, page_of(prot));
	/* An occupied span: the POSIX call answers a bare hint by relocating,
	 * and the mapper discriminates occupancy by the address coming back
	 * changed, so a refusal at the hint becomes an allocation anywhere. */
	if (!got && addr)
		got = VirtualAlloc(NULL, len, MEM_RESERVE | MEM_COMMIT,
		                   page_of(prot));
	if (!got) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return got;
}

int mprotect(void *addr, size_t len, int prot)
{
	DWORD old;
	if (!VirtualProtect(addr, len, page_of(prot), &old)) {
		errno = EACCES;
		return -1;
	}
	return 0;
}

int munmap(void *addr, size_t len)
{
	(void) len;
	if (!VirtualFree(addr, 0, MEM_RELEASE)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
