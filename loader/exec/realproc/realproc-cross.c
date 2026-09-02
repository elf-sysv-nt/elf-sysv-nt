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
#include <stdint.h>

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

/*
 * The map/enter path's error reporting reads errno to name why a faced call
 * refused. In the crossing host errno is the faced runtime's -- *__errno_location()
 * where __errno_location is a System V export -- so a direct read crosses the
 * DR-0066 boundary the wrong way and faults inside fail() before it can report.
 * rp_errno rides the same sysv_abi thunk the mmap seam uses, resolving
 * __errno_location from elfsysv1.dll and dereferencing it; an absent export or a
 * null location reads as errno 0 rather than faulting, so a report degrades to a
 * missing errno instead of a crash.
 */
int rp_errno(void)
{
typedef int *(__attribute__((sysv_abi)) *errloc_fn)(void);
static errloc_fn p;
int *loc;
if (!p)
p = (errloc_fn)(void *)GetProcAddress(
GetModuleHandleA("elfsysv1.dll"), "__errno_location");
if (!p)
return 0;
loc = p();
return loc ? *loc : 0;
}

/*
 * Who already holds the address a fixed-address image wants. The placement
 * refusal used to report only that the span was occupied, which is true and
 * useless: an occupant that can be rebased is a link-time fix, one the NT
 * loader insists on placing low is a two-process bootstrap, and the diagnostic
 * could not tell them apart. VirtualQuery gives the region, GetMappedFileName
 * gives its backing file, and the pair names the module.
 *
 * Returns a pointer to static storage, overwritten by the next call, and never
 * NULL -- the caller is a failure path and must not acquire a second way to
 * fail. GetMappedFileName lives in psapi.dll and is re-exported by kernel32 as
 * K32GetMappedFileNameA on everything this project supports; the psapi fallback
 * is there because the re-export is a Vista-era convenience, not an ABI
 * guarantee. It yields an NT device path (\Device\HarddiskVolume3\...), which
 * is left as it comes: converting it would need the faced runtime's path
 * machinery, and this runs on the path where that machinery is what failed.
 */
const char *rp_map_owner(const void *addr)
{
typedef DWORD (WINAPI *gmfn_fn)(HANDLE, LPVOID, LPSTR, DWORD);
static gmfn_fn gmfn;
static int resolved;
static char out[MAX_PATH + 64];
char path[MAX_PATH];
MEMORY_BASIC_INFORMATION mbi;
const char *state, *kind;

if (VirtualQuery(addr, &mbi, sizeof mbi) != sizeof mbi)
return "unqueryable";
if (mbi.State == MEM_FREE)
return "free";
state = mbi.State == MEM_RESERVE ? "reserved" : "committed";

switch (mbi.Type) {
case MEM_IMAGE:  kind = "image"; break;
case MEM_MAPPED: kind = "mapping"; break;
default:         kind = "private"; break;
}
/* Private memory is backed by no file, so there is no name to fetch and the
 * absence is itself the finding: an anonymous commitment is not a module that
 * could be rebased. Report the region's extent, which is what distinguishes a
 * stray allocation from a deliberate claim over the whole window. */
if (mbi.Type != MEM_IMAGE && mbi.Type != MEM_MAPPED) {
rp_snprintf(out, sizeof out, "%s %s, 0x%llx bytes based at 0x%llx",
	    state, kind, (unsigned long long) mbi.RegionSize,
	    (unsigned long long)(uintptr_t) mbi.AllocationBase);
return out;
}

if (!resolved) {
resolved = 1;
gmfn = (gmfn_fn)(void *)GetProcAddress(
GetModuleHandleA("kernel32.dll"), "K32GetMappedFileNameA");
if (!gmfn) {
HMODULE psapi = LoadLibraryA("psapi.dll");
if (psapi)
gmfn = (gmfn_fn)(void *)GetProcAddress(psapi, "GetMappedFileNameA");
}
}
path[0] = '\0';
if (!gmfn || !gmfn(GetCurrentProcess(), mbi.AllocationBase, path, MAX_PATH)) {
rp_snprintf(out, sizeof out, "%s %s at 0x%llx, unnamed",
	    state, kind,
	    (unsigned long long)(uintptr_t) mbi.AllocationBase);
return out;
}
rp_snprintf(out, sizeof out, "%s %s at 0x%llx, %s",
	    state, kind,
	    (unsigned long long)(uintptr_t) mbi.AllocationBase, path);
return out;
}

#endif /* ELFSYSV_REALPROC */
