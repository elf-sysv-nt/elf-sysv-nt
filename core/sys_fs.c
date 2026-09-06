/*
 * sys_fs.c -- the file syscalls: the shape each one has at the boundary,
 * the copies across it, and the VFS call that does the work.
 *
 * Every user address arrives as a number and leaves as one.  Paths are
 * copied in through the substrate's user_copy_in before the VFS sees them;
 * data moves through a kernel bounce buffer in chunks (0012 § 4: never hand
 * a host call a user address, under either substrate); results are copied
 * out the same way.  A fault on either copy is EFAULT, and the VFS never
 * learns it happened.
 */
#include <stdlib.h>
#include <string.h>

#include "syscall.h"
#include "sys_fs.h"
#include "task.h"
#include "vfs.h"
#include "lxtypes.h"
#include "lxerrno.h"

#define BOUNCE		65536

static unsigned char *bounce(void)
{
	static unsigned char *b;
	if (!b) b = malloc(BOUNCE);
	return b;
}

/* ---- crossing the boundary --------------------------------------------------- */

static int copy_in(struct substrate *s, void *dst, uint64_t uaddr, size_t len)
{
	uint64_t fault = 0;
	if (len == 0) return 0;
	return s->user_copy_in(s, dst, uaddr, len, &fault) == 0 ? 0 : -EFAULT;
}

static int copy_out(struct substrate *s, uint64_t uaddr, const void *src, size_t len)
{
	uint64_t fault = 0;
	if (len == 0) return 0;
	return s->user_copy_out(s, uaddr, src, len, &fault) == 0 ? 0 : -EFAULT;
}

/* A NUL-terminated path from user memory into buf; -EFAULT, or
 * -ENAMETOOLONG past LX_PATH_MAX - 1. */
static int copy_path(struct substrate *s, uint64_t uaddr, char *buf)
{
	size_t got = 0;
	while (got < LX_PATH_MAX) {
		size_t chunk = 64;
		uint64_t fault = 0;
		const void *nul;
		if (got + chunk > LX_PATH_MAX) chunk = LX_PATH_MAX - got;
		/* a page boundary may sit inside the chunk and the string may end
		 * before it: shrink the chunk to the page when the whole fails */
		if (s->user_copy_in(s, buf + got, uaddr + got, chunk, &fault) != 0) {
			size_t k;
			for (k = 0; k < chunk; k++) {
				if (s->user_copy_in(s, buf + got + k, uaddr + got + k, 1, &fault) != 0)
					return -EFAULT;
				if (buf[got + k] == 0) return 0;
			}
			got += chunk;
			continue;
		}
		nul = memchr(buf + got, 0, chunk);
		if (nul) return 0;
		got += chunk;
	}
	return -ENAMETOOLONG;
}

/* Where a *at call starts: the working directory, or the directory behind
 * dirfd.  Sets *self to the descriptor's own file for AT_EMPTY_PATH with an
 * empty path.  0 or -errno. */
static int base_of(int dirfd, const char *path, struct vfs_base *b, struct file **self)
{
	struct file *f;
	b->base_path = NULL;
	if (self) *self = NULL;
	if (path[0] == '/') return 0;
	if (dirfd == AT_FDCWD) return 0;
	f = fd_get(&current->fdt, dirfd);
	if (!f) return -EBADF;
	if (path[0] == 0) { if (self) *self = f; return 0; }
	if (f->kind != FILE_KIND_HOSTDIR && !(f->kind == FILE_KIND_PROC && f->ops->getdents) &&
	    !(f->kind == FILE_KIND_DEV && f->ops->getdents))
		return -ENOTDIR;
	b->base_path = f->path;
	return 0;
}

static struct file *fget(int fd)
{
	return fd_get(&current->fdt, fd);
}

/* ---- stat shapes ----------------------------------------------------------------- */

static void to_stat(const struct kstat *k, struct lx_stat *st)
{
	memset(st, 0, sizeof *st);
	st->st_dev = k->dev;
	st->st_ino = k->ino;
	st->st_nlink = k->nlink;
	st->st_mode = k->mode;
	st->st_uid = k->uid;
	st->st_gid = k->gid;
	st->st_rdev = ((uint64_t)(k->rdev_major & 0xfff) << 8) | (k->rdev_minor & 0xff) |
		      ((uint64_t)(k->rdev_minor & ~0xffU) << 12) | ((uint64_t)(k->rdev_major & ~0xfffU) << 32);
	st->st_size = (int64_t)k->size;
	st->st_blksize = k->blksize;
	st->st_blocks = (int64_t)k->blocks;
	st->st_atim.tv_sec = k->atime / 1000000000; st->st_atim.tv_nsec = k->atime % 1000000000;
	st->st_mtim.tv_sec = k->mtime / 1000000000; st->st_mtim.tv_nsec = k->mtime % 1000000000;
	st->st_ctim.tv_sec = k->ctime / 1000000000; st->st_ctim.tv_nsec = k->ctime % 1000000000;
}

static void to_statx(const struct kstat *k, unsigned mask, struct lx_statx *st)
{
	memset(st, 0, sizeof *st);
	st->stx_mask = (mask & STATX_BASIC_STATS) | (mask & STATX_BTIME);
	st->stx_blksize = k->blksize;
	st->stx_nlink = k->nlink;
	st->stx_uid = k->uid;
	st->stx_gid = k->gid;
	st->stx_mode = (uint16_t)k->mode;
	st->stx_ino = k->ino;
	st->stx_size = k->size;
	st->stx_blocks = k->blocks;
	st->stx_atime.tv_sec = k->atime / 1000000000; st->stx_atime.tv_nsec = (uint32_t)(k->atime % 1000000000);
	st->stx_mtime.tv_sec = k->mtime / 1000000000; st->stx_mtime.tv_nsec = (uint32_t)(k->mtime % 1000000000);
	st->stx_ctime.tv_sec = k->ctime / 1000000000; st->stx_ctime.tv_nsec = (uint32_t)(k->ctime % 1000000000);
	st->stx_btime.tv_sec = k->btime / 1000000000; st->stx_btime.tv_nsec = (uint32_t)(k->btime % 1000000000);
	st->stx_rdev_major = k->rdev_major;
	st->stx_rdev_minor = k->rdev_minor;
	st->stx_dev_major = (uint32_t)(k->dev >> 8) & 0xfff;
	st->stx_dev_minor = (uint32_t)(k->dev & 0xff) | (uint32_t)((k->dev >> 12) & 0xfff00);
}

/* ---- descriptors ------------------------------------------------------------------ */

static int64_t sys_close(int fd)
{
	return fd_close(&current->fdt, fd);
}

static int64_t sys_dup(int fd)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	return fd_install(&current->fdt, f, 0, 0);
}

static int64_t sys_dup3(int oldfd, int newfd, int flags)
{
	if (flags & ~O_CLOEXEC) return -EINVAL;
	if (oldfd == newfd) return -EINVAL;
	return fd_dup2(&current->fdt, oldfd, newfd, flags & O_CLOEXEC);
}

static int64_t sys_dup2(int oldfd, int newfd)
{
	if (!fget(oldfd)) return -EBADF;
	if (oldfd == newfd) return newfd;
	return fd_dup2(&current->fdt, oldfd, newfd, 0);
}

static int64_t sys_fcntl(int fd, int cmd, int64_t arg)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	switch (cmd) {
	case F_DUPFD: return fd_install(&current->fdt, f, 0, (int)arg);
	case F_DUPFD_CLOEXEC: return fd_install(&current->fdt, f, 1, (int)arg);
	case F_GETFD: return fd_get_cloexec(&current->fdt, fd);
	case F_SETFD: return fd_set_cloexec(&current->fdt, fd, (int)(arg & FD_CLOEXEC));
	case F_GETFL: return (int64_t)(f->flags & ~(unsigned)O_CLOEXEC);
	case F_SETFL:
		f->flags = (f->flags & ~(unsigned)(O_APPEND | O_NONBLOCK | O_NOATIME | O_DIRECT)) |
			   ((unsigned)arg & (O_APPEND | O_NONBLOCK | O_NOATIME | O_DIRECT));
		return 0;
	default: return -EINVAL;
	}
}

/* ---- bytes ------------------------------------------------------------------------ */

static int64_t do_read(struct substrate *s, struct file *f, uint64_t ubuf, int64_t len, int64_t off, int positional)
{
	unsigned char *b = bounce();
	int64_t done = 0;
	if (len < 0) return -EINVAL;
	if (!b) return -ENOMEM;
	if ((f->flags & O_ACCMODE) == O_WRONLY || (f->flags & O_ACCMODE) == 3) return -EBADF;
	if (f->flags & O_PATH) return -EBADF;
	if (positional && !f->ops->pread) return -ESPIPE;
	if (!positional && !f->ops->read) return -EINVAL;
	while (done < len) {
		int64_t chunk = len - done, n;
		if (chunk > BOUNCE) chunk = BOUNCE;
		n = positional ? f->ops->pread(f, b, (size_t)chunk, (uint64_t)(off + done))
			       : f->ops->read(f, b, (size_t)chunk);
		if (n < 0) return done ? done : n;
		if (n == 0) break;
		if (copy_out(s, ubuf + (uint64_t)done, b, (size_t)n)) return done ? done : -EFAULT;
		done += n;
		if (n < chunk) break;
	}
	return done;
}

static int64_t do_write(struct substrate *s, struct file *f, uint64_t ubuf, int64_t len, int64_t off, int positional)
{
	unsigned char *b = bounce();
	int64_t done = 0;
	if (len < 0) return -EINVAL;
	if (!b) return -ENOMEM;
	if ((f->flags & O_ACCMODE) == O_RDONLY || (f->flags & O_ACCMODE) == 3) return -EBADF;
	if (f->flags & O_PATH) return -EBADF;
	if (positional && !f->ops->pwrite) return -ESPIPE;
	if (!positional && !f->ops->write) return -EINVAL;
	while (done < len) {
		int64_t chunk = len - done, n;
		if (chunk > BOUNCE) chunk = BOUNCE;
		if (copy_in(s, b, ubuf + (uint64_t)done, (size_t)chunk)) return done ? done : -EFAULT;
		n = positional ? f->ops->pwrite(f, b, (size_t)chunk, (uint64_t)(off + done))
			       : f->ops->write(f, b, (size_t)chunk);
		if (n < 0) return done ? done : n;
		done += n;
		if (n < chunk) break;
	}
	return done;
}

static int64_t sys_readv_writev(struct substrate *s, int fd, uint64_t uiov, int64_t cnt, int writing)
{
	struct file *f = fget(fd);
	struct lx_iovec *iov;
	int64_t total = 0, i;
	if (!f) return -EBADF;
	if (cnt < 0 || cnt > 1024) return -EINVAL;
	iov = malloc((size_t)(cnt ? cnt : 1) * sizeof *iov);
	if (!iov) return -ENOMEM;
	if (copy_in(s, iov, uiov, (size_t)cnt * sizeof *iov)) { free(iov); return -EFAULT; }
	for (i = 0; i < cnt; i++) {
		int64_t n;
		if (iov[i].iov_len == 0) continue;
		if (iov[i].iov_len > 0x7fffffff) { free(iov); return total ? total : -EINVAL; }
		n = writing ? do_write(s, f, iov[i].iov_base, (int64_t)iov[i].iov_len, 0, 0)
			    : do_read(s, f, iov[i].iov_base, (int64_t)iov[i].iov_len, 0, 0);
		if (n < 0) { free(iov); return total ? total : n; }
		total += n;
		if ((uint64_t)n < iov[i].iov_len) break;
	}
	free(iov);
	return total;
}

static int64_t sys_lseek(int fd, int64_t off, int whence)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	if (!f->ops->lseek) return -ESPIPE;
	return f->ops->lseek(f, off, whence);
}

static int64_t sys_getdents64(struct substrate *s, int fd, uint64_t ubuf, int64_t len)
{
	struct file *f = fget(fd);
	unsigned char *b = bounce();
	int64_t n;
	if (!f) return -EBADF;
	if (!f->ops->getdents) return -ENOTDIR;
	if (len < 0) return -EINVAL;
	if (len > BOUNCE) len = BOUNCE;
	if (!b) return -ENOMEM;
	n = f->ops->getdents(f, b, (size_t)len);
	if (n <= 0) return n;
	if (copy_out(s, ubuf, b, (size_t)n)) return -EFAULT;
	return n;
}

/* ---- opening and the paths ------------------------------------------------------- */

static int64_t sys_openat(struct substrate *s, int dirfd, uint64_t upath, int flags, uint32_t mode)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	struct file *f = NULL, *self = NULL;
	int r;
	r = copy_path(s, upath, path);
	if (r) return r;
	r = base_of(dirfd, path, &b, &self);
	if (r) return r;
	if (self || !path[0]) return -ENOENT;
	r = vfs_open(&current->fs, &b, path, (unsigned)flags, mode, &f);
	if (r) return r;
	r = fd_install(&current->fdt, f, flags & O_CLOEXEC, 0);
	file_put(f);
	return r;
}

static int64_t sys_fstatat(struct substrate *s, int dirfd, uint64_t upath, uint64_t ust, int flags, int isx, unsigned mask)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	struct file *self = NULL;
	struct kstat k;
	int r;
	if (upath) {
		r = copy_path(s, upath, path);
		if (r) return r;
	} else {
		path[0] = 0;
	}
	r = base_of(dirfd, path, &b, &self);
	if (r) return r;
	if (!path[0]) {
		if (!(flags & AT_EMPTY_PATH)) return -ENOENT;
		if (!self) { self = fget(dirfd); if (!self) return -EBADF; }
		r = vfs_fstat(self, &k);
	} else {
		r = vfs_stat(&current->fs, &b, path, !(flags & AT_SYMLINK_NOFOLLOW), &k);
	}
	if (r) return r;
	if (isx) {
		struct lx_statx sx;
		to_statx(&k, mask, &sx);
		return copy_out(s, ust, &sx, sizeof sx);
	} else {
		struct lx_stat st;
		to_stat(&k, &st);
		return copy_out(s, ust, &st, sizeof st);
	}
}

static int64_t sys_fstat(struct substrate *s, int fd, uint64_t ust)
{
	struct file *f = fget(fd);
	struct kstat k;
	struct lx_stat st;
	int r;
	if (!f) return -EBADF;
	r = vfs_fstat(f, &k);
	if (r) return r;
	to_stat(&k, &st);
	return copy_out(s, ust, &st, sizeof st);
}

static int64_t sys_faccessat(struct substrate *s, int dirfd, uint64_t upath, int mode, int flags)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, upath, path);
	if (r) return r;
	if (mode & ~7) return -EINVAL;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_access(&current->fs, &b, path, mode, flags & AT_EACCESS);
}

static int64_t sys_readlinkat(struct substrate *s, int dirfd, uint64_t upath, uint64_t ubuf, int64_t cap)
{
	char path[LX_PATH_MAX], target[LX_PATH_MAX];
	struct vfs_base b;
	int64_t n;
	int r = copy_path(s, upath, path);
	if (r) return r;
	if (cap <= 0) return -EINVAL;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	if (!path[0]) return -ENOENT;
	n = vfs_readlink(&current->fs, &b, path, target, sizeof target);
	if (n < 0) return n;
	if (n > cap) n = cap;
	if (copy_out(s, ubuf, target, (size_t)n)) return -EFAULT;
	return n;
}

static int64_t sys_symlinkat(struct substrate *s, uint64_t utarget, int dirfd, uint64_t upath)
{
	char target[LX_PATH_MAX], path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, utarget, target);
	if (r) return r;
	r = copy_path(s, upath, path);
	if (r) return r;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_symlink(&current->fs, target, &b, path);
}

static int64_t sys_linkat(struct substrate *s, int odirfd, uint64_t uold, int ndirfd, uint64_t unew, int flags)
{
	char old[LX_PATH_MAX], new_[LX_PATH_MAX];
	struct vfs_base ob, nb;
	struct file *self = NULL;
	int r = copy_path(s, uold, old);
	if (r) return r;
	r = copy_path(s, unew, new_);
	if (r) return r;
	if (flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH)) return -EINVAL;
	r = base_of(odirfd, old, &ob, &self);
	if (r) return r;
	if (!old[0]) {
		if (!(flags & AT_EMPTY_PATH) || !self) return -ENOENT;
		/* link the descriptor's own path */
		ob.base_path = NULL;
		strcpy(old, self->path);
	}
	r = base_of(ndirfd, new_, &nb, NULL);
	if (r) return r;
	return vfs_link(&current->fs, &ob, old, &nb, new_, flags & AT_SYMLINK_FOLLOW);
}

static int64_t sys_unlinkat(struct substrate *s, int dirfd, uint64_t upath, int flags)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, upath, path);
	if (r) return r;
	if (flags & ~AT_REMOVEDIR) return -EINVAL;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_unlink(&current->fs, &b, path, flags & AT_REMOVEDIR);
}

static int64_t sys_renameat2(struct substrate *s, int odirfd, uint64_t uold, int ndirfd, uint64_t unew, unsigned flags)
{
	char old[LX_PATH_MAX], new_[LX_PATH_MAX];
	struct vfs_base ob, nb;
	int r = copy_path(s, uold, old);
	if (r) return r;
	r = copy_path(s, unew, new_);
	if (r) return r;
	r = base_of(odirfd, old, &ob, NULL);
	if (r) return r;
	r = base_of(ndirfd, new_, &nb, NULL);
	if (r) return r;
	return vfs_rename(&current->fs, &ob, old, &nb, new_, flags);
}

static int64_t sys_mkdirat(struct substrate *s, int dirfd, uint64_t upath, uint32_t mode)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, upath, path);
	if (r) return r;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_mkdir(&current->fs, &b, path, mode);
}

static int64_t sys_mknodat(struct substrate *s, int dirfd, uint64_t upath, uint32_t mode, uint64_t dev)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, upath, path);
	if (r) return r;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_mknod(&current->fs, &b, path, mode,
			 (uint32_t)(((dev >> 8) & 0xfff) | ((dev >> 32) & ~0xfffULL)),
			 (uint32_t)((dev & 0xff) | ((dev >> 12) & ~0xffULL)));
}

static int64_t sys_fchmodat(struct substrate *s, int dirfd, uint64_t upath, uint32_t mode)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	int r = copy_path(s, upath, path);
	if (r) return r;
	r = base_of(dirfd, path, &b, NULL);
	if (r) return r;
	return vfs_chmod(&current->fs, &b, path, mode);
}

static int64_t sys_fchmod(int fd, uint32_t mode)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	return vfs_fchmod(f, mode);
}

static int64_t sys_fchownat(struct substrate *s, int dirfd, uint64_t upath, uint32_t uid, uint32_t gid, int flags)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	struct file *self = NULL;
	int r = copy_path(s, upath, path);
	if (r) return r;
	if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) return -EINVAL;
	r = base_of(dirfd, path, &b, &self);
	if (r) return r;
	if (!path[0]) {
		if (!(flags & AT_EMPTY_PATH) || !self) return -ENOENT;
		return vfs_fchown(self, uid, gid);
	}
	return vfs_chown(&current->fs, &b, path, uid, gid, !(flags & AT_SYMLINK_NOFOLLOW));
}

static int64_t sys_fchown(int fd, uint32_t uid, uint32_t gid)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	return vfs_fchown(f, uid, gid);
}

static int64_t sys_utimensat(struct substrate *s, int dirfd, uint64_t upath, uint64_t utimes, int flags)
{
	char path[LX_PATH_MAX];
	struct vfs_base b;
	struct lx_timespec ts[2];
	int64_t at, mt;
	int r;
	if (flags & ~AT_SYMLINK_NOFOLLOW) return -EINVAL;
	if (utimes) {
		if (copy_in(s, ts, utimes, sizeof ts)) return -EFAULT;
		if (ts[0].tv_nsec == UTIME_NOW) at = VFS_TIME_NOW;
		else if (ts[0].tv_nsec == UTIME_OMIT) at = VFS_TIME_OMIT;
		else if (ts[0].tv_nsec < 0 || ts[0].tv_nsec >= 1000000000) return -EINVAL;
		else at = ts[0].tv_sec * 1000000000 + ts[0].tv_nsec;
		if (ts[1].tv_nsec == UTIME_NOW) mt = VFS_TIME_NOW;
		else if (ts[1].tv_nsec == UTIME_OMIT) mt = VFS_TIME_OMIT;
		else if (ts[1].tv_nsec < 0 || ts[1].tv_nsec >= 1000000000) return -EINVAL;
		else mt = ts[1].tv_sec * 1000000000 + ts[1].tv_nsec;
	} else {
		at = mt = VFS_TIME_NOW;
	}
	if (upath) {
		r = copy_path(s, upath, path);
		if (r) return r;
		r = base_of(dirfd, path, &b, NULL);
		if (r) return r;
		if (!path[0]) return -ENOENT;
		return vfs_utimens(&current->fs, &b, path, at, mt, !(flags & AT_SYMLINK_NOFOLLOW));
	} else {
		struct file *f = fget(dirfd);
		if (!f) return -EBADF;
		return vfs_futimens(f, at, mt);
	}
}

static int64_t sys_truncate(struct substrate *s, uint64_t upath, int64_t size)
{
	char path[LX_PATH_MAX];
	struct vfs_base b = { NULL };
	int r = copy_path(s, upath, path);
	if (r) return r;
	if (size < 0) return -EINVAL;
	return vfs_truncate(&current->fs, &b, path, (uint64_t)size);
}

static int64_t sys_ftruncate(int fd, int64_t size)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	if (size < 0) return -EINVAL;
	if ((f->flags & O_ACCMODE) == O_RDONLY) return -EINVAL;
	if (!f->ops->truncate) return -EINVAL;
	return f->ops->truncate(f, (uint64_t)size);
}

static int64_t sys_fsync(int fd)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	if (!f->ops->fsync) return -EINVAL;
	return f->ops->fsync(f);
}

static int64_t sys_chdir(struct substrate *s, uint64_t upath)
{
	char path[LX_PATH_MAX];
	struct vfs_base b = { NULL };
	int r = copy_path(s, upath, path);
	if (r) return r;
	return vfs_chdir(&current->fs, &b, path);
}

static int64_t sys_fchdir(int fd)
{
	struct file *f = fget(fd);
	if (!f) return -EBADF;
	return vfs_fchdir(&current->fs, f);
}

static int64_t sys_getcwd(struct substrate *s, uint64_t ubuf, int64_t cap)
{
	char cwd[LX_PATH_MAX];
	int64_t n;
	if (cap <= 0) return -EINVAL;
	n = vfs_getcwd(&current->fs, cwd, sizeof cwd);
	if (n < 0) return n;
	if (n > cap) return -ERANGE;
	if (copy_out(s, ubuf, cwd, (size_t)n)) return -EFAULT;
	return n;
}

static int64_t sys_pipe2(struct substrate *s, uint64_t ufds, int flags)
{
	struct file *rd, *wr;
	int fds[2], r;
	if (flags & ~(O_NONBLOCK | O_CLOEXEC)) return -EINVAL;
	r = vfs_pipe(&rd, &wr, (unsigned)flags);
	if (r) return r;
	fds[0] = fd_install(&current->fdt, rd, flags & O_CLOEXEC, 0);
	fds[1] = fds[0] < 0 ? fds[0] : fd_install(&current->fdt, wr, flags & O_CLOEXEC, 0);
	file_put(rd);
	file_put(wr);
	if (fds[0] < 0) return fds[0];
	if (fds[1] < 0) { fd_close(&current->fdt, fds[0]); return fds[1]; }
	if (copy_out(s, ufds, fds, sizeof fds)) {
		fd_close(&current->fdt, fds[0]);
		fd_close(&current->fdt, fds[1]);
		return -EFAULT;
	}
	return 0;
}

static int64_t sys_umask(uint32_t mask)
{
	uint32_t old = current->fs.umask;
	current->fs.umask = mask & 0777;
	return old;
}

/* ---- the table ---------------------------------------------------------------------- */

int64_t sys_fs_dispatch(struct substrate *s, const struct sysframe *f, int *handled)
{
	*handled = 1;
	switch (f->nr) {
	case NR_read: { struct file *fl = fget((int)f->a1); return fl ? do_read(s, fl, (uint64_t)f->a2, f->a3, 0, 0) : -EBADF; }
	case NR_write: { struct file *fl = fget((int)f->a1); return fl ? do_write(s, fl, (uint64_t)f->a2, f->a3, 0, 0) : -EBADF; }
	case NR_pread64: { struct file *fl = fget((int)f->a1); if (f->a4 < 0) return -EINVAL; return fl ? do_read(s, fl, (uint64_t)f->a2, f->a3, f->a4, 1) : -EBADF; }
	case NR_pwrite64: { struct file *fl = fget((int)f->a1); if (f->a4 < 0) return -EINVAL; return fl ? do_write(s, fl, (uint64_t)f->a2, f->a3, f->a4, 1) : -EBADF; }
	case NR_readv: return sys_readv_writev(s, (int)f->a1, (uint64_t)f->a2, f->a3, 0);
	case NR_writev: return sys_readv_writev(s, (int)f->a1, (uint64_t)f->a2, f->a3, 1);
	case NR_open: return sys_openat(s, AT_FDCWD, (uint64_t)f->a1, (int)f->a2, (uint32_t)f->a3);
	case NR_openat: return sys_openat(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3, (uint32_t)f->a4);
	case NR_creat: return sys_openat(s, AT_FDCWD, (uint64_t)f->a1, O_CREAT | O_WRONLY | O_TRUNC, (uint32_t)f->a2);
	case NR_close: return sys_close((int)f->a1);
	case NR_stat: return sys_fstatat(s, AT_FDCWD, (uint64_t)f->a1, (uint64_t)f->a2, 0, 0, 0);
	case NR_lstat: return sys_fstatat(s, AT_FDCWD, (uint64_t)f->a1, (uint64_t)f->a2, AT_SYMLINK_NOFOLLOW, 0, 0);
	case NR_fstat: return sys_fstat(s, (int)f->a1, (uint64_t)f->a2);
	case NR_newfstatat: return sys_fstatat(s, (int)f->a1, (uint64_t)f->a2, (uint64_t)f->a3, (int)f->a4, 0, 0);
	case NR_statx: return sys_fstatat(s, (int)f->a1, (uint64_t)f->a2, (uint64_t)f->a5, (int)f->a3, 1, (unsigned)f->a4);
	case NR_lseek: return sys_lseek((int)f->a1, f->a2, (int)f->a3);
	case NR_access: return sys_faccessat(s, AT_FDCWD, (uint64_t)f->a1, (int)f->a2, 0);
	case NR_faccessat: return sys_faccessat(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3, (int)f->a4);
	case NR_pipe: return sys_pipe2(s, (uint64_t)f->a1, 0);
	case NR_pipe2: return sys_pipe2(s, (uint64_t)f->a1, (int)f->a2);
	case NR_dup: return sys_dup((int)f->a1);
	case NR_dup2: return sys_dup2((int)f->a1, (int)f->a2);
	case NR_dup3: return sys_dup3((int)f->a1, (int)f->a2, (int)f->a3);
	case NR_fcntl: return sys_fcntl((int)f->a1, (int)f->a2, f->a3);
	case NR_fsync: case NR_fdatasync: return sys_fsync((int)f->a1);
	case NR_truncate: return sys_truncate(s, (uint64_t)f->a1, f->a2);
	case NR_ftruncate: return sys_ftruncate((int)f->a1, f->a2);
	case NR_getdents64: return sys_getdents64(s, (int)f->a1, (uint64_t)f->a2, f->a3);
	case NR_getcwd: return sys_getcwd(s, (uint64_t)f->a1, f->a2);
	case NR_chdir: return sys_chdir(s, (uint64_t)f->a1);
	case NR_fchdir: return sys_fchdir((int)f->a1);
	case NR_rename: return sys_renameat2(s, AT_FDCWD, (uint64_t)f->a1, AT_FDCWD, (uint64_t)f->a2, 0);
	case NR_renameat: return sys_renameat2(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3, (uint64_t)f->a4, 0);
	case NR_renameat2: return sys_renameat2(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3, (uint64_t)f->a4, (unsigned)f->a5);
	case NR_mkdir: return sys_mkdirat(s, AT_FDCWD, (uint64_t)f->a1, (uint32_t)f->a2);
	case NR_mkdirat: return sys_mkdirat(s, (int)f->a1, (uint64_t)f->a2, (uint32_t)f->a3);
	case NR_rmdir: return sys_unlinkat(s, AT_FDCWD, (uint64_t)f->a1, AT_REMOVEDIR);
	case NR_unlink: return sys_unlinkat(s, AT_FDCWD, (uint64_t)f->a1, 0);
	case NR_unlinkat: return sys_unlinkat(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3);
	case NR_link: return sys_linkat(s, AT_FDCWD, (uint64_t)f->a1, AT_FDCWD, (uint64_t)f->a2, 0);
	case NR_linkat: return sys_linkat(s, (int)f->a1, (uint64_t)f->a2, (int)f->a3, (uint64_t)f->a4, (int)f->a5);
	case NR_symlink: return sys_symlinkat(s, (uint64_t)f->a1, AT_FDCWD, (uint64_t)f->a2);
	case NR_symlinkat: return sys_symlinkat(s, (uint64_t)f->a1, (int)f->a2, (uint64_t)f->a3);
	case NR_readlink: return sys_readlinkat(s, AT_FDCWD, (uint64_t)f->a1, (uint64_t)f->a2, f->a3);
	case NR_readlinkat: return sys_readlinkat(s, (int)f->a1, (uint64_t)f->a2, (uint64_t)f->a3, f->a4);
	case NR_chmod: return sys_fchmodat(s, AT_FDCWD, (uint64_t)f->a1, (uint32_t)f->a2);
	case NR_fchmodat: return sys_fchmodat(s, (int)f->a1, (uint64_t)f->a2, (uint32_t)f->a3);
	case NR_fchmod: return sys_fchmod((int)f->a1, (uint32_t)f->a2);
	case NR_chown: return sys_fchownat(s, AT_FDCWD, (uint64_t)f->a1, (uint32_t)f->a2, (uint32_t)f->a3, 0);
	case NR_lchown: return sys_fchownat(s, AT_FDCWD, (uint64_t)f->a1, (uint32_t)f->a2, (uint32_t)f->a3, AT_SYMLINK_NOFOLLOW);
	case NR_fchownat: return sys_fchownat(s, (int)f->a1, (uint64_t)f->a2, (uint32_t)f->a3, (uint32_t)f->a4, (int)f->a5);
	case NR_fchown: return sys_fchown((int)f->a1, (uint32_t)f->a2, (uint32_t)f->a3);
	case NR_utimensat: return sys_utimensat(s, (int)f->a1, (uint64_t)f->a2, (uint64_t)f->a3, (int)f->a4);
	case NR_mknodat: return sys_mknodat(s, (int)f->a1, (uint64_t)f->a2, (uint32_t)f->a3, (uint64_t)f->a4);
	case NR_umask: return sys_umask((uint32_t)f->a1);
	case NR_getpid: return current->pid;
	case NR_getuid: return current->fs.uid;
	case NR_geteuid: return current->fs.euid;
	case NR_getgid: return current->fs.gid;
	case NR_getegid: return current->fs.egid;
	default: *handled = 0; return -ENOSYS;
	}
}
