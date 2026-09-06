/*
 * vfs.h -- the virtual file system: the mount table, path resolution with
 * Linux's rules, and the file operations over the host store and the
 * in-kernel file systems (0011 § 8).
 *
 * Everything here takes kernel strings and kernel buffers.  A path is walked
 * one component at a time from the process's root or working directory, or
 * from the path a directory descriptor was opened at; LX symlinks are spliced
 * in with `..` resolved lexically over the canonical path, ELOOP at 40, and
 * a mount point changes file systems when the walk steps onto it.  The host
 * side of every operation is one hostfs.h call on a handle the walk opened.
 *
 * In this phase the walk re-opens the directory chain from the mount root
 * for each resolution.  0011 § 8's fast path (the whole path in one NT open,
 * a per-process directory-handle cache) is the optimisation this leaves for
 * when a measurement asks for it.
 */
#ifndef CORE_VFS_H
#define CORE_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "file.h"
#include "hostfs.h"
#include "lxtypes.h"

#define MNT_HOST	1		/* a host directory: the root tree, /mnt/<drive> */
#define MNT_PROC	2
#define MNT_DEV		3

#define MNT_MAX		16

struct mount {
	int type;
	char point[LX_PATH_MAX];		/* "/", "/proc", "/mnt/c"; no trailing slash but for "/" */
	hfs_h root;			/* MNT_HOST: the directory handle the mount is seated on */
	uint64_t dev;			/* st_dev for everything under it */
	int lx_capable;			/* the volume keeps LX attributes and POSIX delete */
	uint32_t default_uid, default_gid;	/* synthesised owner where the EAs are absent */
};

/* The file-system half of a process: mounts, root, working directory,
 * umask and credentials.  Cloned on fork in a later phase. */
struct fs_ctx {
	struct mount mounts[MNT_MAX];
	int nmounts;
	char cwd[LX_PATH_MAX];		/* canonical, absolute */
	uint32_t umask;
	uint32_t uid, gid, euid, egid;
};

/* Seat the mount table: the root tree at host path root, /proc and /dev as
 * in-kernel file systems.  0 or -errno.  Optional host mounts follow. */
int vfs_init(struct fs_ctx *fs, const char *root_host_path);
int vfs_mount_host(struct fs_ctx *fs, const char *point, const char *host_path);
void vfs_release(struct fs_ctx *fs);

/* Where a relative path starts: the working directory, or a descriptor's
 * directory.  base_path is that directory's canonical path. */
struct vfs_base {
	const char *base_path;		/* NULL means the working directory */
};

/* open(2) semantics over flags and mode (the umask already applied by the
 * caller).  On success *out holds one reference. */
int vfs_open(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	     unsigned flags, uint32_t mode, struct file **out);

/* stat, lstat, fstatat: follow says whether the last symlink is followed. */
int vfs_stat(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	     int follow, struct kstat *st);
int vfs_fstat(struct file *f, struct kstat *st);

/* access(2)'s check with the real ids (or the effective ones). */
int vfs_access(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	       int mode, int effective);

int64_t vfs_readlink(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
		     char *buf, size_t cap);
int vfs_symlink(struct fs_ctx *fs, const char *target, const struct vfs_base *b, const char *path);
int vfs_link(struct fs_ctx *fs, const struct vfs_base *ob, const char *oldpath,
	     const struct vfs_base *nb, const char *newpath, int follow);
int vfs_unlink(struct fs_ctx *fs, const struct vfs_base *b, const char *path, int rmdir);
int vfs_rename(struct fs_ctx *fs, const struct vfs_base *ob, const char *oldpath,
	       const struct vfs_base *nb, const char *newpath, unsigned flags);
int vfs_mkdir(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint32_t mode);
int vfs_mknod(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	      uint32_t mode, uint32_t major, uint32_t minor);
int vfs_chmod(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint32_t mode);
int vfs_fchmod(struct file *f, uint32_t mode);
int vfs_chown(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	      uint32_t uid, uint32_t gid, int follow);
int vfs_fchown(struct file *f, uint32_t uid, uint32_t gid);
/* times as ns, or UTIME_NOW / UTIME_OMIT in tv_nsec's place given as
 * VFS_TIME_NOW / VFS_TIME_OMIT */
#define VFS_TIME_NOW	HFS_TIME_NOW
#define VFS_TIME_OMIT	HFS_TIME_OMIT
int vfs_utimens(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
		int64_t atime, int64_t mtime, int follow);
int vfs_futimens(struct file *f, int64_t atime, int64_t mtime);
int vfs_truncate(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint64_t size);
int vfs_chdir(struct fs_ctx *fs, const struct vfs_base *b, const char *path);
int vfs_fchdir(struct fs_ctx *fs, struct file *f);
/* the canonical working directory into buf; its length, or -ERANGE */
int64_t vfs_getcwd(struct fs_ctx *fs, char *buf, size_t cap);

/* The console descriptors 0, 1, 2 the process inherits: a foreign handle. */
struct file *vfs_console_file(int hostfd);

/* pipe(2): two descriptions over one ring; flags is O_NONBLOCK|O_CLOEXEC. */
int vfs_pipe(struct file **rd, struct file **wr, unsigned flags);

/* /proc/self needs to know what is mapped and what ran; the core tells it. */
void vfs_set_exe_path(const char *linux_path);

/* The running process's file-system context and descriptor table, for the
 * kinds that answer about the process itself. */
struct fs_ctx *vfs_current_fs(void);
struct fdtable *vfs_current_fdtable(void);

#endif /* CORE_VFS_H */
