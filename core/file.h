/*
 * file.h -- open file descriptions and the descriptor table (0011 § 9).
 *
 * A descriptor is an index into a per-process table of references to open
 * file descriptions; dup shares the description and therefore the offset;
 * O_CLOEXEC is on the descriptor, not the description.  An open file
 * description is a kernel object with a small set of operations every kind
 * implements -- Cygwin's fhandler shape with the names changed, borrowed on
 * purpose.  The kinds in this phase are the host-backed file and directory,
 * the pipe, the /dev synthetics and the /proc synthetics; the console the
 * process inherited is the foreign handle of § 9, a kind of its own.
 *
 * Every buffer an operation takes is kernel memory: the syscall layer copies
 * through the substrate's user_copy_* first, so nothing beneath it ever sees a
 * user address.
 */
#ifndef CORE_FILE_H
#define CORE_FILE_H

#include <stddef.h>
#include <stdint.h>

#include "lxtypes.h"
#include "hostfs.h"

struct file;
struct mount;

/* What every stat-like call yields inside the kernel, before it is shaped
 * into struct stat or struct statx for the caller. */
struct kstat {
	uint64_t dev, ino;
	uint32_t mode, nlink, uid, gid;
	uint32_t rdev_major, rdev_minor;
	uint64_t size, blocks;
	int64_t atime, mtime, ctime, btime;	/* ns since the epoch */
	uint32_t blksize;
};

struct file_ops {
	/* read and write at the description's offset, advancing it; a kind
	 * without an offset (a pipe, a console) ignores it */
	int64_t (*read)(struct file *f, void *buf, size_t len);
	int64_t (*write)(struct file *f, const void *buf, size_t len);
	/* positional; -ESPIPE where the kind has no positions */
	int64_t (*pread)(struct file *f, void *buf, size_t len, uint64_t off);
	int64_t (*pwrite)(struct file *f, const void *buf, size_t len, uint64_t off);
	int64_t (*lseek)(struct file *f, int64_t off, int whence);
	int (*stat)(struct file *f, struct kstat *st);
	/* fill buf with linux_dirent64 records from the directory's position;
	 * returns the byte count, 0 at the end */
	int64_t (*getdents)(struct file *f, void *buf, size_t len);
	int (*truncate)(struct file *f, uint64_t size);
	int (*fsync)(struct file *f);
	/* the last reference is gone */
	void (*release)(struct file *f);
};

#define FILE_KIND_HOST		1	/* a regular file on the host store */
#define FILE_KIND_HOSTDIR	2	/* a directory on the host store */
#define FILE_KIND_PIPE		3
#define FILE_KIND_DEV		4	/* /dev null, zero, full, urandom */
#define FILE_KIND_PROC		5	/* a generated /proc file */
#define FILE_KIND_CONSOLE	6	/* the foreign handle: the host console */
#define FILE_KIND_SYMLINK	7	/* O_PATH|O_NOFOLLOW on a link */

struct file {
	const struct file_ops *ops;
	int refs;
	int kind;
	unsigned flags;			/* the open(2) flags that live on the description */
	uint64_t pos;
	/* the host-backed kinds */
	hfs_h h;
	struct hfs_dir *dir;
	struct mount *mnt;
	/* the synthetic kinds keep what they need here */
	void *priv;
	int64_t priv_i;
	/* where it was opened, for /proc/self/fd, fchdir and the like */
	char path[LX_PATH_MAX];
};

/* Allocate a description with one reference; NULL on exhaustion. */
struct file *file_new(const struct file_ops *ops, int kind, unsigned flags);
void file_get(struct file *f);
void file_put(struct file *f);

/* The descriptor table.  NR_OPEN_DEFAULT is RLIMIT_NOFILE's soft default. */
#define NR_OPEN_DEFAULT	1024

struct fdtable {
	struct file *files[NR_OPEN_DEFAULT];
	unsigned char cloexec[NR_OPEN_DEFAULT];
};

void fdtable_init(struct fdtable *t);
/* Close every descriptor. */
void fdtable_release(struct fdtable *t);

/* Install f at the lowest free descriptor not below min, taking a reference;
 * returns the descriptor or -EMFILE. */
int fd_install(struct fdtable *t, struct file *f, int cloexec, int min);
/* A borrowed reference to fd's description, or NULL. */
struct file *fd_get(struct fdtable *t, int fd);
/* Close fd; 0 or -EBADF. */
int fd_close(struct fdtable *t, int fd);
/* dup2/dup3: newfd refers to oldfd's description, closing what it held;
 * returns newfd or -errno. */
int fd_dup2(struct fdtable *t, int oldfd, int newfd, int cloexec);
int fd_get_cloexec(struct fdtable *t, int fd);
int fd_set_cloexec(struct fdtable *t, int fd, int on);

#endif /* CORE_FILE_H */
