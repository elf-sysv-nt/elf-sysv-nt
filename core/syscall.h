/*
 * syscall.h -- the syscall table the gate dispatches into.  Phase 1 answers two
 * numbers, write (1) and exit_group (231), and returns -ENOSYS for the rest,
 * which is what a 4.18 kernel does for a number it does not carry.  The numbers
 * and error values are the el8 UAPI's, read from the target headers, not from
 * memory.
 */
#ifndef CORE_SYSCALL_H
#define CORE_SYSCALL_H

#include "gate.h"

/*
 * Dispatch one captured syscall against substrate s.  The return value is what
 * the gate hands back in %rax: a count, or a negative errno.  exit_group does
 * not return through here -- it ends the run and parks its thread.
 */
long syscall_dispatch(struct substrate *s, const struct sysframe *f);

#endif /* CORE_SYSCALL_H */
