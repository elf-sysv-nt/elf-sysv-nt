/*
 * WP-56 reent-tls-bringup, item 1: the real-process stub's two crossings into
 * the faced runtime. Needs windows.h and, at run time, elfsysv1.dll; compiled
 * only into the real-process build and certified against the faced runtime
 * (t/run.sh cross stage), which SKIPs when that build product is absent.
 *
 *   1. The startup bridge. crt0's _cygwin_crt0_common calls
 *      cygwin_internal(CW_USER_DATA) Microsoft-style; elfsysv1.dll exports it
 *      as a System V veneer, so the unbridged call faults before main.
 *      Re-crossing sysv_abi reaches main -- the -DBRIDGE shape
 *      spike/reent-stub-realproc-window pinned.
 *
 *   2. Output. rp_puts rides a sysv_abi thunk resolved from elfsysv1.dll's
 *      export directory -- the direction spike/reent-stub-libc-crossing found
 *      does cross, a reent-consuming stdio body included.
 */
#ifdef ELFSYSV_REALPROC

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "realproc.h"

/* -nostdlib drops the CRT's memset/memcpy; the compiler still emits calls to
 * them, so supply freestanding copies rather than let them become a
 * Microsoft-into-System-V call into the faced libc. */
void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

unsigned long long cygwin_internal(unsigned int t, ...)
{
typedef unsigned long long (__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
static cw_fn p;
if (!p)
p = (cw_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
return p ? p(t) : 0;
}

int rp_puts(const char *s)
{
typedef int (__attribute__((sysv_abi)) *puts_fn)(const char *);
static puts_fn p;
if (!p)
p = (puts_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "puts");
return p ? p(s) : -1;
}

int rp_eputs(const char *s)
{
typedef long (__attribute__((sysv_abi)) *write_fn)(int, const void *, unsigned long);
static write_fn p;
if (!p)
p = (write_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "write");
if (!p)
return -1;
return (int)p(2, s, (unsigned long)rp_strlen(s));
}

/*
 * The map/enter path's memory primitives. elf_map places the image through
 * mmap/mprotect/munmap; in the crossing host those are the faced runtime's,
 * so a direct Microsoft-ABI call crosses the DR-0066 boundary the wrong way
 * and faults. Each rides a sysv_abi thunk resolved from elfsysv1.dll's export
 * directory, the same direction rp_puts/rp_eputs cross. off is 64-bit to match
 * the System V mmap(2) off_t; failure returns the libc sentinels the callers
 * already test (MAP_FAILED == (void *)-1, -1) so an absent export reads as an
 * ordinary map failure rather than a fault.
 */
void *rp_mmap(void *addr, size_t len, int prot, int flags, int fd,
	      long long off)
{
typedef void *(__attribute__((sysv_abi)) *mmap_fn)(void *, size_t, int, int,
						   int, long long);
static mmap_fn p;
if (!p)
p = (mmap_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "mmap");
if (!p)
return (void *)-1;
return p(addr, len, prot, flags, fd, off);
}

int rp_mprotect(void *addr, size_t len, int prot)
{
typedef int (__attribute__((sysv_abi)) *mprotect_fn)(void *, size_t, int);
static mprotect_fn p;
if (!p)
p = (mprotect_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "mprotect");
if (!p)
return -1;
return p(addr, len, prot);
}

int rp_munmap(void *addr, size_t len)
{
typedef int (__attribute__((sysv_abi)) *munmap_fn)(void *, size_t);
static munmap_fn p;
if (!p)
p = (munmap_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "munmap");
if (!p)
return -1;
return p(addr, len);
}

#endif /* ELFSYSV_REALPROC */
