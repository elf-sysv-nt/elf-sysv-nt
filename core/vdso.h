/*
 * vdso.h -- the kernel-provided object mapped into every process.
 *
 * Linux maps a small shared object into each address space and points
 * AT_SYSINFO_EHDR at it.  glibc reads it before anything else: it parses the
 * ELF header, walks the dynamic symbol table, and binds the handful of calls
 * the kernel is willing to answer in user space.  A process without one still
 * runs -- glibc falls back to the syscall for each -- so this is a mapping the
 * platform owes and not one it can be blocked on.
 *
 * What is built here is a real ELF object with correct headers and a SONAME of
 * `linux-vdso.so.1`, mapped read-execute and published in the auxiliary vector.
 * It exports no functions.  Every call glibc would like to find in it --
 * clock_gettime, gettimeofday, time, getcpu -- needs a clock the kernel does
 * not have until the phase that brings one, and a vDSO that exported a symbol
 * whose body was not there would be worse than an empty one: glibc would bind
 * it and call it.
 */
#ifndef CORE_VDSO_H
#define CORE_VDSO_H

#include <stdint.h>

#include "substrate.h"

/*
 * Map the vDSO into s's address space and record it in the VMA tree.  On
 * success *base is the user address of its ELF header, which is what goes in
 * AT_SYSINFO_EHDR.  Returns 0, or a negative value if the mapping fails.
 */
int vdso_map(struct substrate *s, uint64_t *base);

#endif /* CORE_VDSO_H */
