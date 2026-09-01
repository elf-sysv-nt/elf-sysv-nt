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

#endif /* ELFSYSV_REALPROC */
