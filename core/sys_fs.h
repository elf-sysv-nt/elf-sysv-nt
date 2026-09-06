/*
 * sys_fs.h -- the file syscalls' entry from the table.
 */
#ifndef CORE_SYS_FS_H
#define CORE_SYS_FS_H

#include "gate.h"

/* Answer one captured syscall if it is a file syscall; *handled says whether
 * it was one.  The return value is the gate's %rax. */
int64_t sys_fs_dispatch(struct substrate *s, const struct sysframe *f, int *handled);

#endif /* CORE_SYS_FS_H */
