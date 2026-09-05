/*
 * procfs.h -- /proc, as far as criterion 1 needs it.
 *
 * One file exists: /proc/self/maps.  It is generated at open time from the VMA
 * tree and read from that snapshot, which is what Linux does and what a reader
 * depends on -- a map that changed under a partial read would hand back two
 * halves of different address spaces.
 *
 * This is not the VFS.  0011 puts the VFS, the mount table and the descriptor
 * layer in Phase 2, and when they arrive /proc becomes one filesystem among
 * them, reached through the same open/read/close the rest of the tree uses.
 * What is here is the descriptor table those syscalls need and nothing else,
 * kept small enough to be replaced rather than grown into.
 */
#ifndef CORE_PROCFS_H
#define CORE_PROCFS_H

#include <stddef.h>
#include <stdint.h>

/* Forget every open file.  Called with vma_reset before a process is built. */
void procfs_reset(void);

/*
 * Open a path.  Returns a descriptor >= 3, or a negative Linux errno: -ENOENT
 * for a path that is not one of the generated files, -EMFILE when the table is
 * full.  Descriptors 0, 1 and 2 are the host console and are never handed out
 * here.
 */
int procfs_open(const char *path);

/*
 * Read up to len bytes from fd into dst.  Returns the count, 0 at end of file,
 * or -EBADF for a descriptor this table did not hand out.
 */
long procfs_read(int fd, void *dst, size_t len);

/* Close fd.  Returns 0 or -EBADF. */
int procfs_close(int fd);

/* Whether fd belongs to this table, so write can tell a file from the console. */
int procfs_owns(int fd);

#endif /* CORE_PROCFS_H */
