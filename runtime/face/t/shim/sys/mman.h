/* WP-27: a Win32 stand-in for the POSIX memory calls beneath the mapper, so
 * the certified elf_map.c compiles native for the sole-runtime elfcall
 * certification. Only what elf_map.c calls is here, with the semantics it
 * relies on: an anonymous private mmap reserves and commits zeroed pages at
 * the hinted base, exactly there or relocated; mprotect changes a committed
 * range; munmap releases a whole allocation by its base. In the product the
 * stub is a program of the faced runtime and these calls are that runtime's
 * own; this shim exists so the certification does not have to wait for that
 * program to exist.
 */
#ifndef ELFSYSV_FACE_T_SHIM_MMAN_H
#define ELFSYSV_FACE_T_SHIM_MMAN_H

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

#define MAP_FAILED ((void *) -1)

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t len, int prot, int flags, int fd,
           long long off);
int mprotect(void *addr, size_t len, int prot);
int munmap(void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_FACE_T_SHIM_MMAN_H */
