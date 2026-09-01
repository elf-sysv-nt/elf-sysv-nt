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

buf = VirtualAlloc(NULL, len ? len : 1, MEM_RESERVE | MEM_COMMIT,
   PAGE_READWRITE);
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
