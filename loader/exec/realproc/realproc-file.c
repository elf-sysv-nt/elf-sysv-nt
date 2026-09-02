/*
 * WP-56 reent-tls-bringup, item 1: the real-process stub's image file read,
 * host-safe.
 *
 * `slurp` reads the whole ELF image (and the --elf-runtime) into memory before
 * the loader maps it. In the real-process shape a plain fopen/fread would be a
 * Microsoft-into-System-V call into the faced libc that returns without
 * crossing (spike/reent-stub-realproc-window), so the read is done here with
 * Win32 directly -- CreateFileA / GetFileSizeEx / ReadFile / VirtualAlloc --
 * calling no libc at all. Reading the image is the stub's own input work, the
 * host-safe side of DR-0066's line; only output crosses (realproc-cross.c).
 *
 * The window is reserved before slurp runs (stub.c holds ELF_WINDOW_BASE
 * before loading anything), so a VirtualAlloc for the scratch buffer cannot
 * land where the image must go.
 *
 * Compiled only into the real-process build; the plain-PE build the WP-41
 * exec-* certifications drive reads through the identity seam in realproc.h
 * (the libc fopen/fread it always used). Certified natively -- the read carries
 * no crossing -- by t/run.sh's `file` stage.
 */
#ifdef ELFSYSV_REALPROC

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "realproc.h"
#include "reserve.h"

/*
 * Scratch memory, taken above the low window rather than wherever the host
 * feels like putting it. VirtualAlloc(NULL, ...) is satisfied out of the
 * lowest free region, and in this process the lowest free region is the low
 * window itself, so the buffer holding the image landed exactly where the
 * image had to go -- a span whose size tracked the file's, which is how it was
 * finally recognised. The scan walks free regions from the top of the window
 * upward and takes the first that fits; a failure returns NULL and reads as an
 * ordinary read failure, since a buffer below the line is not an acceptable
 * fallback under DR-0072.
 */
static unsigned char *alloc_above_window(size_t len)
{
MEMORY_BASIC_INFORMATION m;
uint64_t at = ELF_WINDOW_BASE + ELF_WINDOW_SIZE;
void *p;

while (at < UINT64_C(0x7ff000000000)) {
if (!VirtualQuery((void *)(UINT_PTR) at, &m, sizeof m))
break;
if (m.State == MEM_FREE && (uint64_t) m.RegionSize >= (uint64_t) len) {
p = VirtualAlloc((void *)(UINT_PTR) at, len,
 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
if (p)
return (unsigned char *) p;
}
at = (uint64_t)(UINT_PTR) m.BaseAddress + (uint64_t) m.RegionSize;
}
return NULL;
}

unsigned char *rp_slurp(const char *path, size_t *size, int *err)
{
HANDLE h;
LARGE_INTEGER li;
unsigned char *buf;
size_t len, off;

h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
if (h == INVALID_HANDLE_VALUE) { *err = RP_SLURP_OPEN; return NULL; }

if (!GetFileSizeEx(h, &li) || li.QuadPart < 0) {
*err = RP_SLURP_SIZE; CloseHandle(h); return NULL;
}
len = (size_t) li.QuadPart;

buf = alloc_above_window(len ? len : 1);
if (!buf) { *err = RP_SLURP_READ; CloseHandle(h); return NULL; }

/* ReadFile takes a DWORD count; loop so a >4G image is not truncated. */
for (off = 0; off < len; ) {
size_t want = len - off;
DWORD chunk = want > 0x40000000u ? 0x40000000u : (DWORD) want;
DWORD got = 0;
if (!ReadFile(h, buf + off, chunk, &got, NULL) || got == 0) {
*err = RP_SLURP_READ;
VirtualFree(buf, 0, MEM_RELEASE);
CloseHandle(h);
return NULL;
}
off += got;
}

CloseHandle(h);
*size = len;
return buf;
}

#endif /* ELFSYSV_REALPROC */
