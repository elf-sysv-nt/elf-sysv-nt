/*
 * vfs.c -- the mount table, the walk, and the host-backed file kinds.
 *
 * The walk is the part with rules in it.  A path is consumed a component at a
 * time against a canonical absolute path that never contains ".", ".." or a
 * symlink: "." is dropped, ".." is lexical over the canonical path (which is
 * what Linux does once a symlink has been resolved into it), and an LX
 * symlink met along the way is read and its target spliced in front of what
 * remains, forty times at most.  Stepping onto a mount point changes the
 * mount; the host store never sees a path that crosses one.  Every host
 * access is then an open of one component under a directory handle the walk
 * holds, which is the race-free shape 0011 § 8 asks for within a component.
 */
#include <stdlib.h>
#include <string.h>

#include "vfs.h"
#include "vfs_internal.h"
#include "lxerrno.h"

/* ---- path arithmetic ---------------------------------------------------- */

void path_parent(char *abs)
{
	char *s = strrchr(abs, '/');
	if (!s || s == abs) { abs[1] = 0; return; }
	*s = 0;
}

int path_join(char *out, size_t cap, const char *dir, const char *name)
{
	size_t dl = strlen(dir), nl = strlen(name);
	if (dl + 1 + nl + 1 > cap) return -ENAMETOOLONG;
	if (dl == 1 && dir[0] == '/') {
		out[0] = '/';
		memcpy(out + 1, name, nl + 1);
	} else {
		memcpy(out, dir, dl);
		out[dl] = '/';
		memcpy(out + dl + 1, name, nl + 1);
	}
	return 0;
}

const char *path_rel(const struct mount *m, const char *abs)
{
	size_t pl = strlen(m->point);
	if (pl == 1) return abs[1] ? abs + 1 : "";
	if (strlen(abs) == pl) return "";
	return abs + pl + 1;
}

/* ---- mounts -------------------------------------------------------------- */

static struct mount *mount_of(struct fs_ctx *fs, const char *abs)
{
	struct mount *best = NULL;
	size_t bl = 0;
	int i;
	for (i = 0; i < fs->nmounts; i++) {
		struct mount *m = &fs->mounts[i];
		size_t pl = strlen(m->point);
		int hit = pl == 1 ? 1
			: (strncmp(abs, m->point, pl) == 0 && (abs[pl] == 0 || abs[pl] == '/'));
		if (hit && pl >= bl) { best = m; bl = pl; }
	}
	return best;
}

static struct mount *mount_at(struct fs_ctx *fs, const char *abs)
{
	int i;
	for (i = 0; i < fs->nmounts; i++)
		if (strcmp(fs->mounts[i].point, abs) == 0) return &fs->mounts[i];
	return NULL;
}

static int add_mount(struct fs_ctx *fs, int type, const char *point)
{
	struct mount *m;
	if (fs->nmounts >= MNT_MAX) return -ENOMEM;
	m = &fs->mounts[fs->nmounts];
	memset(m, 0, sizeof *m);
	m->type = type;
	strncpy(m->point, point, LX_PATH_MAX - 1);
	m->dev = (uint64_t)(fs->nmounts + 1);	/* the synthetics take small numbers */
	fs->nmounts++;
	return 0;
}

int vfs_mount_host(struct fs_ctx *fs, const char *point, const char *host_path)
{
	struct mount *m;
	int r = add_mount(fs, MNT_HOST, point);
	if (r) return r;
	m = &fs->mounts[fs->nmounts - 1];
	r = hfs_open_root(host_path, &m->root);
	if (r) { fs->nmounts--; return r; }
	m->dev = hfs_volume_serial(m->root);
	if (!m->dev) m->dev = 0x1000 + (uint64_t)fs->nmounts;
	m->lx_capable = hfs_volume_is_lx_capable(m->root) == 1;
	m->default_uid = 0;
	m->default_gid = 0;
	/* the root tree is case-sensitive per directory (0011 § 8); the
	 * installer marks it, and so does the kernel for what it creates; a
	 * drive mounted under /mnt keeps Windows's rule */
	if (strcmp(point, "/") == 0 && m->lx_capable)
		hfs_set_case_sensitive(m->root);
	return 0;
}

int vfs_init(struct fs_ctx *fs, const char *root_host_path)
{
	int r;
	memset(fs, 0, sizeof *fs);
	strcpy(fs->cwd, "/");
	fs->umask = 022;
	r = vfs_mount_host(fs, "/", root_host_path);
	if (r) return r;
	r = add_mount(fs, MNT_PROC, "/proc");
	if (r) return r;
	r = add_mount(fs, MNT_DEV, "/dev");
	if (r) return r;
	return 0;
}

void vfs_release(struct fs_ctx *fs)
{
	int i;
	for (i = 0; i < fs->nmounts; i++)
		if (fs->mounts[i].type == MNT_HOST && fs->mounts[i].root)
			hfs_close(fs->mounts[i].root);
	fs->nmounts = 0;
}

/* ---- stat shaping -------------------------------------------------------- */

static uint32_t mode_of(const struct mount *m, const struct hfs_stat *hs, const char *name)
{
	uint32_t type, perm;
	switch (hs->kind) {
	case HFS_KIND_DIR: type = S_IFDIR; break;
	case HFS_KIND_LXLINK: type = S_IFLNK; break;
	case HFS_KIND_FIFO: type = S_IFIFO; break;
	case HFS_KIND_CHR: type = S_IFCHR; break;
	case HFS_KIND_BLK: type = S_IFBLK; break;
	case HFS_KIND_SOCK: type = S_IFSOCK; break;
	default: type = S_IFREG; break;
	}
	if (hs->lxflags & HFS_LX_MODE) {
		perm = hs->mode & 07777;
		/* the type is NTFS's to say: an EA left behind by a mknod over a
		 * later plain file must not turn it into a device */
		return type | perm;
	}
	(void)m;
	if (type == S_IFDIR) perm = 0755;
	else if (type == S_IFLNK) perm = 0777;
	else {
		size_t l = name ? strlen(name) : 0;
		perm = 0644;
		if (l > 4 && (!strcmp(name + l - 4, ".exe") || !strcmp(name + l - 4, ".bat") ||
			      !strcmp(name + l - 4, ".cmd") || !strcmp(name + l - 4, ".com")))
			perm = 0755;
	}
	return type | perm;
}

static void kstat_of(const struct mount *m, const struct hfs_stat *hs, const char *name, struct kstat *st)
{
	memset(st, 0, sizeof *st);
	st->dev = m->dev;
	st->ino = hs->ino;
	st->mode = mode_of(m, hs, name);
	st->nlink = hs->nlink ? hs->nlink : 1;
	st->uid = (hs->lxflags & HFS_LX_UID) ? hs->uid : m->default_uid;
	st->gid = (hs->lxflags & HFS_LX_GID) ? hs->gid : m->default_gid;
	if (hs->lxflags & HFS_LX_DEV) { st->rdev_major = hs->rdev_major; st->rdev_minor = hs->rdev_minor; }
	st->size = hs->size;
	st->blocks = hs->blocks;
	st->blksize = 4096;
	st->atime = hs->atime;
	st->mtime = hs->mtime;
	st->ctime = hs->ctime;
	st->btime = hs->btime;
	if (hs->kind == HFS_KIND_LXLINK) {
		/* a symlink's size is its target's length; the reparse object's
		 * bytes are zero, so read it when asked */
		st->size = 0;
	}
}

void kstat_synthetic(struct kstat *st, uint64_t dev, uint64_t ino, uint32_t mode, uint64_t size)
{
	int64_t now = hfs_now();
	memset(st, 0, sizeof *st);
	st->dev = dev;
	st->ino = ino;
	st->mode = mode;
	st->nlink = 1;
	st->size = size;
	st->blksize = 4096;
	st->atime = st->mtime = st->ctime = st->btime = now;
}

/* ---- permission ------------------------------------------------------------ */

/* Linux's discretionary check.  mask is R_OK|W_OK|X_OK. */
static int may(const struct fs_ctx *fs, const struct kstat *st, int mask, int effective)
{
	uint32_t uid = effective ? fs->euid : fs->uid, gid = effective ? fs->egid : fs->gid;
	uint32_t perm;
	if (uid == 0) {
		/* root: everything but execute, which needs some x bit on a file */
		if ((mask & X_OK) && !S_ISDIR(st->mode) && !(st->mode & 0111)) return -EACCES;
		return 0;
	}
	if (uid == st->uid) perm = (st->mode >> 6) & 7;
	else if (gid == st->gid) perm = (st->mode >> 3) & 7;
	else perm = st->mode & 7;
	if ((mask & (uint32_t)perm) != (uint32_t)mask) return -EACCES;
	return 0;
}

/* ---- opening the directory chain on the host ------------------------------ */

/* Open the host directory for a canonical path under a host mount: walk
 * the relative components from the mount root, each an open under the
 * previous.  The path is canonical so no component is an LX symlink; a
 * Windows link is followed by NTFS. */
static int open_host_dir(struct mount *m, const char *abs, hfs_h *out)
{
	const char *rel = path_rel(m, abs);
	hfs_h cur = m->root, next;
	char comp[LX_NAME_MAX + 1];
	int owned = 0, r;
	while (*rel) {
		const char *s = strchr(rel, '/');
		size_t n = s ? (size_t)(s - rel) : strlen(rel);
		if (n > LX_NAME_MAX) { if (owned) hfs_close(cur); return -ENAMETOOLONG; }
		memcpy(comp, rel, n); comp[n] = 0;
		r = hfs_openat(cur, comp, HFS_O_READ | HFS_O_DIR, 0, 0, 0, &next, NULL);
		if (owned) hfs_close(cur);
		if (r == HFS_REPARSE) { hfs_close(next); return -ENOTDIR; }
		if (r < 0) return r;
		cur = next; owned = 1;
		rel = s ? s + 1 : rel + n;
	}
	if (!owned) {
		/* the mount root itself: hand back a duplicate so the caller can
		 * close it uniformly */
		r = hfs_reopen(m->root, HFS_O_READ | HFS_O_DIR, &cur, NULL);
		if (r < 0) return r;
	}
	*out = cur;
	return 0;
}

/* ---- the walk ------------------------------------------------------------- */

#define W_PARENT	0x1		/* stop at the parent of the last component */
#define W_NOFOLLOW	0x2		/* do not follow a symlink as the last component */
#define W_MUSTDIR	0x4		/* the object must be a directory (trailing slash) */

/* Stat one canonical object on its mount. */
static int stat_abs(struct fs_ctx *fs, const char *abs, struct kstat *st, uint32_t *kind)
{
	struct mount *m = mount_of(fs, abs);
	const char *rel;
	if (!m) return -ENOENT;
	rel = path_rel(m, abs);
	if (m->type == MNT_PROC) { if (kind) *kind = 0; return procfs_stat(fs, rel, st); }
	if (m->type == MNT_DEV) { if (kind) *kind = 0; return devfs_stat(fs, rel, st); }
	if (!*rel) {
		struct hfs_stat hs;
		int r = hfs_stat(m->root, &hs);
		if (r) return r;
		kstat_of(m, &hs, NULL, st);
		if (kind) *kind = hs.kind;
		return 0;
	} else {
		char parent[LX_PATH_MAX];
		const char *name;
		hfs_h d;
		struct hfs_stat hs;
		int r;
		strcpy(parent, abs);
		path_parent(parent);
		name = strrchr(abs, '/') + 1;
		r = open_host_dir(m, parent, &d);
		if (r) return r;
		r = hfs_statat(d, name, &hs);
		hfs_close(d);
		if (r) return r;
		kstat_of(m, &hs, name, st);
		if (kind) *kind = hs.kind;
		return 0;
	}
}

/* Read the target of the LX symlink at canonical abs. */
static int64_t readlink_abs(struct fs_ctx *fs, const char *abs, char *buf, size_t cap)
{
	struct mount *m = mount_of(fs, abs);
	char parent[LX_PATH_MAX];
	const char *name;
	hfs_h d, l;
	int r;
	int64_t n;
	if (!m) return -ENOENT;
	if (m->type == MNT_PROC) return procfs_readlink(fs, path_rel(m, abs), buf, cap);
	if (m->type == MNT_DEV) return devfs_readlink(fs, path_rel(m, abs), buf, cap);
	if (!*path_rel(m, abs)) return -EINVAL;
	strcpy(parent, abs);
	path_parent(parent);
	name = strrchr(abs, '/') + 1;
	r = open_host_dir(m, parent, &d);
	if (r) return r;
	r = hfs_openat(d, name, HFS_O_READ | HFS_O_REPARSE, 0, 0, 0, &l, NULL);
	hfs_close(d);
	if (r < 0) return r;
	n = hfs_readlink(l, buf, cap);
	hfs_close(l);
	return n;
}

static int walk(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
		unsigned wflags, struct vfs_loc *loc)
{
	char abs[LX_PATH_MAX], rest[LX_PATH_MAX], comp[LX_NAME_MAX + 1];
	const char *p;
	int nlinks = 0, trailing = 0;
	size_t plen = strlen(path);

	if (plen == 0) return -ENOENT;
	if (plen >= LX_PATH_MAX) return -ENAMETOOLONG;
	if (path[0] == '/') strcpy(abs, "/");
	else strncpy(abs, (b && b->base_path) ? b->base_path : fs->cwd, LX_PATH_MAX);
	abs[LX_PATH_MAX - 1] = 0;
	strcpy(rest, path);
	if (plen > 1 && path[plen - 1] == '/') trailing = 1;
	memset(loc, 0, sizeof *loc);

	p = rest;
	for (;;) {
		const char *s;
		size_t n;
		int last;
		while (*p == '/') p++;
		if (!*p) break;
		s = strchr(p, '/');
		n = s ? (size_t)(s - p) : strlen(p);
		if (n > LX_NAME_MAX) return -ENAMETOOLONG;
		memcpy(comp, p, n); comp[n] = 0;
		{
			const char *q = s ? s : p + n;
			while (*q == '/') q++;
			last = (*q == 0);
		}
		/* the object so far must be a directory to go further */
		if (strcmp(comp, ".") == 0) { p = s ? s + 1 : p + n; continue; }
		if (strcmp(comp, "..") == 0) { path_parent(abs); p = s ? s + 1 : p + n; continue; }
		if (last && (wflags & W_PARENT)) {
			loc->m = mount_of(fs, abs);
			strcpy(loc->abs, abs);
			strcpy(loc->last, comp);
			loc->rel = path_rel(loc->m, loc->abs);
			/* the parent must exist and be a directory */
			{
				struct kstat st;
				int r = stat_abs(fs, abs, &st, NULL);
				if (r) return r;
				if (!S_ISDIR(st.mode)) return -ENOTDIR;
			}
			return 0;
		}
		{
			char cand[LX_PATH_MAX];
			struct kstat st;
			uint32_t kind = 0;
			int r = path_join(cand, sizeof cand, abs, comp);
			if (r) return r;
			if (mount_at(fs, cand)) { strcpy(abs, cand); p = s ? s + 1 : p + n; continue; }
			r = stat_abs(fs, cand, &st, &kind);
			if (r) return r;
			if (S_ISLNK(st.mode) && (!last || !(wflags & W_NOFOLLOW) || trailing)) {
				char target[LX_PATH_MAX], newrest[LX_PATH_MAX];
				int64_t tl;
				size_t remain;
				if (++nlinks > 40) return -ELOOP;
				tl = readlink_abs(fs, cand, target, sizeof target - 1);
				if (tl < 0) return (int)tl;
				if (tl == 0 || tl >= (int64_t)sizeof target) return -ENOENT;
				target[tl] = 0;
				remain = s ? strlen(s) : 0;
				if ((size_t)tl + remain + 1 >= sizeof newrest) return -ENAMETOOLONG;
				memcpy(newrest, target, (size_t)tl);
				if (s) memcpy(newrest + tl, s, remain);
				newrest[tl + remain] = 0;
				strcpy(rest, newrest);
				p = rest;
				if (target[0] == '/') strcpy(abs, "/");
				continue;
			}
			if (!last && !S_ISDIR(st.mode)) return -ENOTDIR;
			strcpy(abs, cand);
		}
		p = s ? s + 1 : p + n;
	}
	loc->m = mount_of(fs, abs);
	strcpy(loc->abs, abs);
	loc->rel = path_rel(loc->m, loc->abs);
	if (wflags & W_PARENT) return -EEXIST;	/* the path named the root or resolved to it */
	if ((wflags & W_MUSTDIR) || trailing) {
		struct kstat st;
		int r = stat_abs(fs, abs, &st, NULL);
		if (r) return r;
		if (!S_ISDIR(st.mode)) return -ENOTDIR;
	}
	return 0;
}

/* ---- the host-backed kinds ----------------------------------------------------- */

static int64_t hostfile_read(struct file *f, void *buf, size_t len)
{
	int64_t n = hfs_pread(f->h, buf, len, f->pos);
	if (n > 0) f->pos += (uint64_t)n;
	return n;
}

static int64_t hostfile_write(struct file *f, const void *buf, size_t len)
{
	int64_t n;
	if (f->flags & O_APPEND) {
		struct hfs_stat hs;
		n = hfs_pwrite(f->h, buf, len, 0, 1);
		if (n > 0 && hfs_stat(f->h, &hs) == 0) f->pos = hs.size;
		return n;
	}
	n = hfs_pwrite(f->h, buf, len, f->pos, 0);
	if (n > 0) f->pos += (uint64_t)n;
	return n;
}

static int64_t hostfile_pread(struct file *f, void *buf, size_t len, uint64_t off)
{
	return hfs_pread(f->h, buf, len, off);
}

static int64_t hostfile_pwrite(struct file *f, const void *buf, size_t len, uint64_t off)
{
	/* Linux appends on pwrite too when the description has O_APPEND; the
	 * offset is ignored, and the man page says so under BUGS */
	return hfs_pwrite(f->h, buf, len, off, (f->flags & O_APPEND) ? 1 : 0);
}

static int64_t hostfile_lseek(struct file *f, int64_t off, int whence)
{
	int64_t base;
	struct hfs_stat hs;
	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = (int64_t)f->pos; break;
	case SEEK_END:
		if (hfs_stat(f->h, &hs)) return -EIO;
		base = (int64_t)hs.size;
		break;
	case SEEK_DATA:
	case SEEK_HOLE:
		if (hfs_stat(f->h, &hs)) return -EIO;
		if (off < 0 || (uint64_t)off >= hs.size) return -ENXIO;
		f->pos = whence == SEEK_DATA ? (uint64_t)off : hs.size;
		return (int64_t)f->pos;
	default: return -EINVAL;
	}
	if (base + off < 0) return -EINVAL;
	f->pos = (uint64_t)(base + off);
	return (int64_t)f->pos;
}

static int hostfile_stat(struct file *f, struct kstat *st)
{
	struct hfs_stat hs;
	const char *name = strrchr(f->path, '/');
	int r = hfs_stat(f->h, &hs);
	if (r) return r;
	kstat_of(f->mnt, &hs, name ? name + 1 : NULL, st);
	if (f->kind == FILE_KIND_SYMLINK) {
		char t[LX_PATH_MAX];
		int64_t n = hfs_readlink(f->h, t, sizeof t);
		if (n > 0) st->size = (uint64_t)n;
	}
	return 0;
}

static int hostfile_truncate(struct file *f, uint64_t size)
{
	return hfs_truncate(f->h, size);
}

static int hostfile_fsync(struct file *f)
{
	return hfs_fsync(f->h);
}

static void hostfile_release(struct file *f)
{
	if (f->dir) hfs_dir_close(f->dir);
	if (f->h) hfs_close(f->h);
}

static uint8_t dtype_of(uint32_t kind)
{
	switch (kind) {
	case HFS_KIND_DIR: return DT_DIR;
	case HFS_KIND_LXLINK: return DT_LNK;
	case HFS_KIND_FIFO: return DT_FIFO;
	case HFS_KIND_CHR: return DT_CHR;
	case HFS_KIND_BLK: return DT_BLK;
	case HFS_KIND_SOCK: return DT_SOCK;
	default: return DT_REG;
	}
}

int64_t dirent_pack(void *buf, size_t cap, size_t *used, uint64_t ino, int64_t off,
		    uint8_t type, const char *name)
{
	size_t nl = strlen(name);
	size_t rec = (LX_DIRENT64_HDR + nl + 1 + 7) & ~(size_t)7;
	uint16_t reclen = (uint16_t)rec;
	unsigned char *p = (unsigned char *)buf + *used;
	if (*used + rec > cap) return -EINVAL;
	/* the header is 19 bytes and the name follows it unpadded, which no C
	 * struct of these fields lays out; write the fields at their offsets */
	memcpy(p, &ino, 8);
	memcpy(p + 8, &off, 8);
	memcpy(p + 16, &reclen, 2);
	p[18] = type;
	memcpy(p + LX_DIRENT64_HDR, name, nl + 1);
	memset(p + LX_DIRENT64_HDR + nl + 1, 0, rec - LX_DIRENT64_HDR - nl - 1);
	*used += rec;
	return (int64_t)rec;
}

/* The directory's position: 0 and 1 are the synthesised "." and "..", 2
 * onward is the host's own cursor, which only moves forward; a seek to 0
 * restarts it.  Entries a call had no room for are held in priv for the
 * next, so the host reader is never asked to give one back. */
static int64_t hostdir_getdents(struct file *f, void *buf, size_t len)
{
	size_t used = 0;
	struct hfs_dirent *held;
	int restart = 0;
	if (!f->priv) {
		f->priv = calloc(32, sizeof(struct hfs_dirent));
		if (!f->priv) return -ENOMEM;
	}
	held = f->priv;
	if (f->pos == 0) {
		struct hfs_stat hs;
		uint64_t ino = 0;
		if (hfs_stat(f->h, &hs) == 0) ino = hs.ino;
		if (dirent_pack(buf, len, &used, ino, 1, DT_DIR, ".") < 0) return -EINVAL;
		f->pos = 1;
	}
	if (f->pos == 1) {
		if (dirent_pack(buf, len, &used, 1, 2, DT_DIR, "..") < 0) return (int64_t)used;
		f->pos = 2;
		restart = 1;
	}
	if (!f->dir) {
		f->dir = hfs_dir_open(f->h);
		if (!f->dir) return -ENOMEM;
		restart = 1;
	}
	for (;;) {
		int n;
		while (f->priv_i > 0) {
			if (dirent_pack(buf, len, &used, held[0].ino, (int64_t)f->pos + 1,
					dtype_of(held[0].kind), held[0].name) < 0)
				return used ? (int64_t)used : -EINVAL;
			f->priv_i--;
			memmove(held, held + 1, (size_t)f->priv_i * sizeof *held);
			f->pos++;
		}
		n = hfs_dir_read(f->dir, held, 32, restart);
		restart = 0;
		if (n < 0) return used ? (int64_t)used : n;
		if (n == 0) break;
		f->priv_i = n;
	}
	return (int64_t)used;
}

static int64_t hostdir_lseek(struct file *f, int64_t off, int whence)
{
	if (whence == SEEK_SET && off == 0) {
		f->pos = 0;
		f->priv_i = 0;
		if (f->dir) { hfs_dir_close(f->dir); f->dir = NULL; }
		return 0;
	}
	if (whence == SEEK_CUR && off == 0) return (int64_t)f->pos;
	return -EINVAL;
}

/* A directory description has no read: Linux answers EINVAL, and EBADF for
 * a write, since the description could only have been opened read-only. */
static int64_t dir_read(struct file *f, void *buf, size_t len)
{
	(void)f; (void)buf; (void)len;
	return -EINVAL;
}

static int64_t dir_write(struct file *f, const void *buf, size_t len)
{
	(void)f; (void)buf; (void)len;
	return -EBADF;
}

static void hostdir_release(struct file *f)
{
	free(f->priv);
	f->priv = NULL;
	hostfile_release(f);
}

static const struct file_ops hostfile_ops = {
	hostfile_read, hostfile_write, hostfile_pread, hostfile_pwrite, hostfile_lseek,
	hostfile_stat, NULL, hostfile_truncate, hostfile_fsync, hostfile_release,
};

static const struct file_ops hostdir_ops = {
	dir_read, dir_write, NULL, NULL, hostdir_lseek,
	hostfile_stat, hostdir_getdents, NULL, hostfile_fsync, hostdir_release,
};

/* ---- the operations ------------------------------------------------------------ */

static int vfs_open_depth(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
			  unsigned flags, uint32_t mode, struct file **out, int depth);

int vfs_open(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	     unsigned flags, uint32_t mode, struct file **out)
{
	return vfs_open_depth(fs, b, path, flags, mode, out, 0);
}

static int vfs_open_depth(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
			  unsigned flags, uint32_t mode, struct file **out, int depth)
{
	struct vfs_loc loc;
	struct file *f;
	unsigned acc = flags & O_ACCMODE;
	unsigned hflags = 0;
	int r, creating = 0;
	struct hfs_stat hs;
	hfs_h d = 0, h = 0;

	*out = NULL;
	if (depth > 40) return -ELOOP;
	if (flags & O_CREAT) {
		r = walk(fs, b, path, W_PARENT, &loc);
		if (r == -EEXIST) {
			/* the path resolved to an existing directory (the root
			 * or a mount point): open it as a directory */
			r = walk(fs, b, path, 0, &loc);
			if (r) return r;
		} else if (r) {
			return r;
		} else {
			creating = 1;
		}
	} else {
		r = walk(fs, b, path, (flags & O_NOFOLLOW) ? W_NOFOLLOW : 0, &loc);
		if (r) return r;
	}
	if (loc.m->type == MNT_PROC) return creating ? -EACCES : procfs_open(fs, loc.rel, flags, out);
	if (loc.m->type == MNT_DEV) return creating ? -EACCES : devfs_open(fs, loc.rel, flags, out);

	if (acc == O_RDONLY || acc == O_RDWR) hflags |= HFS_O_READ;
	if (acc == O_WRONLY || acc == O_RDWR) hflags |= HFS_O_WRITE;
	if (flags & O_PATH) hflags = HFS_O_ATTR;
	if (flags & O_DIRECTORY) hflags |= HFS_O_DIR;
	if (flags & O_TRUNC) hflags |= HFS_O_TRUNC;

	if (creating) {
		hflags |= HFS_O_CREATE | HFS_O_NODIR;
		if (flags & O_EXCL) hflags |= HFS_O_EXCL;
		r = open_host_dir(loc.m, loc.abs, &d);
		if (r) return r;
		/* O_CREAT on an existing symlink follows it unless O_EXCL */
		r = hfs_openat(d, loc.last, hflags, mode & 07777 & ~fs->umask, fs->euid, fs->egid, &h, &hs);
		if (r == HFS_REPARSE) {
			/* O_CREAT met a symlink: Linux creates through it (the
			 * target may not exist yet), unless O_EXCL, which is
			 * EEXIST on the link itself */
			char target[LX_PATH_MAX];
			struct vfs_base lb = { loc.abs };
			int64_t tl;
			hfs_close(d);
			if (flags & O_EXCL) { hfs_close(h); return -EEXIST; }
			if (hs.kind != HFS_KIND_LXLINK) { hfs_close(h); return -ENXIO; }
			tl = hfs_readlink(h, target, sizeof target - 1);
			hfs_close(h);
			if (tl <= 0 || tl >= (int64_t)sizeof target) return -ENOENT;
			target[tl] = 0;
			return vfs_open_depth(fs, &lb, target, flags, mode, out, depth + 1);
		}
		hfs_close(d);
		if (r < 0) return r;
		if (hs.kind == HFS_KIND_DIR) { hfs_close(h); return -EISDIR; }
	} else {
		struct kstat st;
		uint32_t kind = 0;
open_existing:
		r = stat_abs(fs, loc.abs, &st, &kind);
		if (r) return r;
		if (S_ISLNK(st.mode)) {
			/* O_NOFOLLOW on a symlink: O_PATH opens the link itself,
			 * anything else is ELOOP */
			if (!(flags & O_PATH)) return -ELOOP;
		}
		if (S_ISDIR(st.mode) && (acc != O_RDONLY) && !(flags & O_PATH)) return -EISDIR;
		if (!S_ISDIR(st.mode) && (flags & O_DIRECTORY)) return -ENOTDIR;
		if (!(flags & O_PATH)) {
			int mask = 0;
			if (acc == O_RDONLY || acc == O_RDWR) mask |= R_OK;
			if (acc == O_WRONLY || acc == O_RDWR || (flags & O_TRUNC)) mask |= W_OK;
			r = may(fs, &st, mask, 1);
			if (r) return r;
		}
		if (S_ISDIR(st.mode)) hflags = (hflags & ~(HFS_O_WRITE | HFS_O_TRUNC)) | HFS_O_DIR | HFS_O_READ;
		if (S_ISLNK(st.mode)) hflags |= HFS_O_REPARSE;
		if (!*loc.rel) {
			r = hfs_reopen(loc.m->root, hflags, &h, &hs);
		} else {
			char parent[LX_PATH_MAX];
			strcpy(parent, loc.abs);
			path_parent(parent);
			r = open_host_dir(loc.m, parent, &d);
			if (r) return r;
			r = hfs_openat(d, strrchr(loc.abs, '/') + 1, hflags, 0, 0, 0, &h, &hs);
			hfs_close(d);
		}
		if (r == HFS_REPARSE) { hfs_close(h); return -ELOOP; }
		if (r < 0) return r;
		if ((hs.kind == HFS_KIND_FIFO || hs.kind == HFS_KIND_CHR || hs.kind == HFS_KIND_BLK ||
		     hs.kind == HFS_KIND_SOCK) && !(flags & O_PATH)) {
			hfs_close(h);
			return -ENXIO;	/* special files on the host store are not opened in this phase */
		}
	}
	f = file_new(hs.kind == HFS_KIND_DIR ? &hostdir_ops : &hostfile_ops,
		     hs.kind == HFS_KIND_DIR ? FILE_KIND_HOSTDIR : (hs.kind == HFS_KIND_LXLINK ? FILE_KIND_SYMLINK : FILE_KIND_HOST),
		     flags & ~(unsigned)(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | O_CLOEXEC));
	if (!f) { hfs_close(h); return -ENOMEM; }
	f->h = h;
	f->mnt = loc.m;
	if (creating) path_join(f->path, sizeof f->path, loc.abs, loc.last);
	else strcpy(f->path, loc.abs);
	*out = f;
	return 0;
}

int vfs_stat(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	     int follow, struct kstat *st)
{
	struct vfs_loc loc;
	int r = walk(fs, b, path, follow ? 0 : W_NOFOLLOW, &loc);
	if (r) return r;
	r = stat_abs(fs, loc.abs, st, NULL);
	if (r) return r;
	if (S_ISLNK(st->mode)) {
		char t[LX_PATH_MAX];
		int64_t n = readlink_abs(fs, loc.abs, t, sizeof t);
		if (n > 0) st->size = (uint64_t)n;
	}
	return 0;
}

int vfs_fstat(struct file *f, struct kstat *st)
{
	if (!f->ops->stat) return -EINVAL;
	return f->ops->stat(f, st);
}

int vfs_access(struct fs_ctx *fs, const struct vfs_base *b, const char *path, int mode, int effective)
{
	struct kstat st;
	int r = vfs_stat(fs, b, path, 1, &st);
	if (r) return r;
	if (mode == F_OK) return 0;
	return may(fs, &st, mode & 7, effective);
}

int64_t vfs_readlink(struct fs_ctx *fs, const struct vfs_base *b, const char *path, char *buf, size_t cap)
{
	struct vfs_loc loc;
	struct kstat st;
	int r = walk(fs, b, path, W_NOFOLLOW, &loc);
	if (r) return r;
	r = stat_abs(fs, loc.abs, &st, NULL);
	if (r) return r;
	if (!S_ISLNK(st.mode)) return -EINVAL;
	return readlink_abs(fs, loc.abs, buf, cap);
}

static int host_parent(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
		       struct vfs_loc *loc, hfs_h *d)
{
	int r = walk(fs, b, path, W_PARENT, loc);
	if (r) return r;
	if (loc->m->type != MNT_HOST) return -EACCES;
	if (strcmp(loc->last, ".") == 0 || strcmp(loc->last, "..") == 0) return -EINVAL;
	return open_host_dir(loc->m, loc->abs, d);
}

int vfs_symlink(struct fs_ctx *fs, const char *target, const struct vfs_base *b, const char *path)
{
	struct vfs_loc loc;
	hfs_h d;
	int r;
	if (!*target) return -ENOENT;
	r = host_parent(fs, b, path, &loc, &d);
	if (r) return r;
	r = hfs_symlink(d, loc.last, target, fs->euid, fs->egid);
	hfs_close(d);
	return r;
}

int vfs_link(struct fs_ctx *fs, const struct vfs_base *ob, const char *oldpath,
	     const struct vfs_base *nb, const char *newpath, int follow)
{
	struct vfs_loc oloc, nloc;
	struct kstat st;
	hfs_h od, oh, nd;
	char parent[LX_PATH_MAX];
	int r;
	r = walk(fs, ob, oldpath, follow ? 0 : W_NOFOLLOW, &oloc);
	if (r) return r;
	if (oloc.m->type != MNT_HOST) return -EPERM;
	r = stat_abs(fs, oloc.abs, &st, NULL);
	if (r) return r;
	if (S_ISDIR(st.mode)) return -EPERM;
	r = host_parent(fs, nb, newpath, &nloc, &nd);
	if (r) return r;
	if (nloc.m != oloc.m) { hfs_close(nd); return -EXDEV; }
	strcpy(parent, oloc.abs);
	path_parent(parent);
	r = open_host_dir(oloc.m, parent, &od);
	if (r) { hfs_close(nd); return r; }
	r = hfs_openat(od, strrchr(oloc.abs, '/') + 1, HFS_O_ATTR | HFS_O_REPARSE, 0, 0, 0, &oh, NULL);
	hfs_close(od);
	if (r < 0) { hfs_close(nd); return r; }
	r = hfs_link(oh, nd, nloc.last);
	hfs_close(oh);
	hfs_close(nd);
	return r;
}

int vfs_unlink(struct fs_ctx *fs, const struct vfs_base *b, const char *path, int rmdir)
{
	struct vfs_loc loc;
	struct kstat st;
	char cand[LX_PATH_MAX];
	hfs_h d;
	int r;
	r = walk(fs, b, path, W_PARENT, &loc);
	if (r == -EEXIST) {
		/* the path resolved to the root, a mount point, or "." */
		size_t pl = strlen(path);
		if (rmdir && pl && path[pl - 1] == '.' && (pl == 1 || path[pl - 2] == '/')) return -EINVAL;
		return rmdir ? -EBUSY : -EISDIR;
	}
	if (r) return r;
	if (loc.m->type != MNT_HOST) return -EACCES;
	if (strcmp(loc.last, ".") == 0) return -EINVAL;
	if (strcmp(loc.last, "..") == 0) return -ENOTEMPTY;
	path_join(cand, sizeof cand, loc.abs, loc.last);
	if (mount_at(fs, cand)) return -EBUSY;
	r = stat_abs(fs, cand, &st, NULL);
	if (r) return r;
	if (rmdir && !S_ISDIR(st.mode)) return -ENOTDIR;
	if (!rmdir && S_ISDIR(st.mode)) return -EISDIR;
	if (rmdir && strcmp(cand, fs->cwd) == 0) { /* Linux allows it; the cwd then dangles */ }
	r = open_host_dir(loc.m, loc.abs, &d);
	if (r) return r;
	r = hfs_unlink(d, loc.last, rmdir);
	hfs_close(d);
	return r;
}

int vfs_rename(struct fs_ctx *fs, const struct vfs_base *ob, const char *oldpath,
	       const struct vfs_base *nb, const char *newpath, unsigned flags)
{
	struct vfs_loc oloc, nloc;
	struct kstat ost, nst;
	char ocand[LX_PATH_MAX], ncand[LX_PATH_MAX];
	hfs_h od, nd;
	int r, nexists;
	if (flags & ~(unsigned)(RENAME_NOREPLACE)) return flags & RENAME_EXCHANGE ? -EINVAL : -EINVAL;
	r = walk(fs, ob, oldpath, W_PARENT, &oloc);
	if (r == -EEXIST) return -EBUSY;
	if (r) return r;
	r = walk(fs, nb, newpath, W_PARENT, &nloc);
	if (r == -EEXIST) return -EBUSY;
	if (r) return r;
	if (oloc.m->type != MNT_HOST || nloc.m->type != MNT_HOST) return -EACCES;
	if (oloc.m != nloc.m) return -EXDEV;
	if (!strcmp(oloc.last, ".") || !strcmp(oloc.last, "..") || !strcmp(nloc.last, ".") || !strcmp(nloc.last, ".."))
		return -EINVAL;
	path_join(ocand, sizeof ocand, oloc.abs, oloc.last);
	path_join(ncand, sizeof ncand, nloc.abs, nloc.last);
	if (mount_at(fs, ocand) || mount_at(fs, ncand)) return -EBUSY;
	r = stat_abs(fs, ocand, &ost, NULL);
	if (r) return r;
	nexists = stat_abs(fs, ncand, &nst, NULL) == 0;
	if (strcmp(ocand, ncand) == 0) return 0;
	/* a directory cannot be moved into itself */
	{
		size_t ol = strlen(ocand);
		if (S_ISDIR(ost.mode) && strncmp(ncand, ocand, ol) == 0 && ncand[ol] == '/') return -EINVAL;
	}
	if (nexists) {
		if ((flags & RENAME_NOREPLACE)) return -EEXIST;
		if (S_ISDIR(ost.mode) && !S_ISDIR(nst.mode)) return -ENOTDIR;
		if (!S_ISDIR(ost.mode) && S_ISDIR(nst.mode)) return -EISDIR;
	}
	r = open_host_dir(oloc.m, oloc.abs, &od);
	if (r) return r;
	r = open_host_dir(nloc.m, nloc.abs, &nd);
	if (r) { hfs_close(od); return r; }
	r = hfs_rename(od, oloc.last, nd, nloc.last, (flags & RENAME_NOREPLACE) ? 1 : 0);
	if (r == -EACCES && nexists && S_ISDIR(nst.mode)) r = -ENOTEMPTY;
	hfs_close(od);
	hfs_close(nd);
	return r;
}

int vfs_mkdir(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint32_t mode)
{
	struct vfs_loc loc;
	hfs_h d;
	int r = walk(fs, b, path, W_PARENT, &loc);
	if (r == -EEXIST) return -EEXIST;
	if (r) return r;
	if (loc.m->type != MNT_HOST) return -EACCES;
	if (!strcmp(loc.last, ".") || !strcmp(loc.last, "..")) return -EEXIST;
	r = open_host_dir(loc.m, loc.abs, &d);
	if (r) return r;
	r = hfs_mkdir(d, loc.last, mode & 01777 & ~fs->umask, fs->euid, fs->egid,
		      loc.m->lx_capable && strcmp(loc.m->point, "/") == 0);
	hfs_close(d);
	return r;
}

int vfs_mknod(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	      uint32_t mode, uint32_t major, uint32_t minor)
{
	struct vfs_loc loc;
	hfs_h d;
	uint32_t kind;
	int r;
	switch (mode & S_IFMT) {
	case 0:
	case S_IFREG: {
		struct file *f;
		r = vfs_open(fs, b, path, O_CREAT | O_EXCL | O_WRONLY, mode & 07777, &f);
		if (r) return r;
		file_put(f);
		return 0;
	}
	case S_IFIFO: kind = HFS_KIND_FIFO; break;
	case S_IFCHR: kind = HFS_KIND_CHR; break;
	case S_IFBLK: kind = HFS_KIND_BLK; break;
	case S_IFSOCK: kind = HFS_KIND_SOCK; break;
	default: return -EINVAL;
	}
	r = host_parent(fs, b, path, &loc, &d);
	if (r == -EEXIST) return -EEXIST;
	if (r) return r;
	r = hfs_mknod(d, loc.last, kind, mode & 07777 & ~fs->umask, fs->euid, fs->egid, major, minor);
	hfs_close(d);
	return r;
}

/* Open the object at a resolved location for attribute changes. */
static int open_attr(struct fs_ctx *fs, const char *abs, hfs_h *h)
{
	struct mount *m = mount_of(fs, abs);
	char parent[LX_PATH_MAX];
	hfs_h d;
	int r;
	if (!m || m->type != MNT_HOST) return -EPERM;
	if (!*path_rel(m, abs)) return hfs_reopen(m->root, HFS_O_ATTR | HFS_O_DIR, h, NULL);
	strcpy(parent, abs);
	path_parent(parent);
	r = open_host_dir(m, parent, &d);
	if (r) return r;
	r = hfs_openat(d, strrchr(abs, '/') + 1, HFS_O_ATTR | HFS_O_REPARSE, 0, 0, 0, h, NULL);
	hfs_close(d);
	return r < 0 ? r : 0;
}

static int chmod_h(struct fs_ctx *fs, hfs_h h, uint32_t mode)
{
	struct hfs_stat hs;
	uint32_t type;
	int r = hfs_stat(h, &hs);
	if (r) return r;
	if (fs->euid != 0 && (hs.lxflags & HFS_LX_UID) && hs.uid != fs->euid) return -EPERM;
	switch (hs.kind) {
	case HFS_KIND_DIR: type = S_IFDIR; break;
	case HFS_KIND_LXLINK: type = S_IFLNK; break;
	case HFS_KIND_FIFO: type = S_IFIFO; break;
	case HFS_KIND_CHR: type = S_IFCHR; break;
	case HFS_KIND_BLK: type = S_IFBLK; break;
	case HFS_KIND_SOCK: type = S_IFSOCK; break;
	default: type = S_IFREG; break;
	}
	/* a file without the uid and gid EAs gets them now, so a later stat
	 * agrees with what chmod saw */
	return hfs_set_lx(h, HFS_LX_MODE | ((hs.lxflags & HFS_LX_UID) ? 0 : HFS_LX_UID) |
			  ((hs.lxflags & HFS_LX_GID) ? 0 : HFS_LX_GID),
			  (hs.lxflags & HFS_LX_UID) ? hs.uid : 0, (hs.lxflags & HFS_LX_GID) ? hs.gid : 0,
			  type | (mode & 07777), 0, 0);
}

int vfs_chmod(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint32_t mode)
{
	struct vfs_loc loc;
	hfs_h h;
	int r = walk(fs, b, path, 0, &loc);
	if (r) return r;
	r = open_attr(fs, loc.abs, &h);
	if (r) return r;
	r = chmod_h(fs, h, mode);
	hfs_close(h);
	return r;
}

/* The description may have been opened without attribute access (O_RDONLY
 * asks NT for none); the object is reopened for it through its handle. */
static int reopen_attr(struct file *f, hfs_h *h)
{
	if (f->kind != FILE_KIND_HOST && f->kind != FILE_KIND_HOSTDIR && f->kind != FILE_KIND_SYMLINK) return -EPERM;
	return hfs_reopen(f->h, HFS_O_ATTR | HFS_O_REPARSE | (f->kind == FILE_KIND_HOSTDIR ? HFS_O_DIR : 0), h, NULL);
}

int vfs_fchmod(struct file *f, uint32_t mode)
{
	hfs_h h;
	int r = reopen_attr(f, &h);
	if (r) return r;
	r = chmod_h(vfs_current_fs(), h, mode);
	hfs_close(h);
	return r;
}

static int chown_h(struct fs_ctx *fs, hfs_h h, uint32_t uid, uint32_t gid)
{
	struct hfs_stat hs;
	uint32_t which = 0;
	int r = hfs_stat(h, &hs);
	if (r) return r;
	if (fs->euid != 0) return -EPERM;
	if (uid != (uint32_t)-1) which |= HFS_LX_UID;
	if (gid != (uint32_t)-1) which |= HFS_LX_GID;
	if (!which) return 0;
	if (!(hs.lxflags & HFS_LX_MODE)) {
		/* give the file its mode too, so the owner it now has is not
		 * paired with a synthesised mode that changes with the name */
		which |= HFS_LX_MODE;
	}
	return hfs_set_lx(h, which, uid, gid,
			  (hs.lxflags & HFS_LX_MODE) ? hs.mode : (hs.kind == HFS_KIND_DIR ? S_IFDIR | 0755 : S_IFREG | 0644), 0, 0);
}

int vfs_chown(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
	      uint32_t uid, uint32_t gid, int follow)
{
	struct vfs_loc loc;
	hfs_h h;
	int r = walk(fs, b, path, follow ? 0 : W_NOFOLLOW, &loc);
	if (r) return r;
	r = open_attr(fs, loc.abs, &h);
	if (r) return r;
	r = chown_h(fs, h, uid, gid);
	hfs_close(h);
	return r;
}

int vfs_fchown(struct file *f, uint32_t uid, uint32_t gid)
{
	hfs_h h;
	int r = reopen_attr(f, &h);
	if (r) return r;
	r = chown_h(vfs_current_fs(), h, uid, gid);
	hfs_close(h);
	return r;
}

int vfs_utimens(struct fs_ctx *fs, const struct vfs_base *b, const char *path,
		int64_t atime, int64_t mtime, int follow)
{
	struct vfs_loc loc;
	hfs_h h;
	int r = walk(fs, b, path, follow ? 0 : W_NOFOLLOW, &loc);
	if (r) return r;
	r = open_attr(fs, loc.abs, &h);
	if (r) return r;
	r = hfs_set_times(h, atime, mtime);
	hfs_close(h);
	return r;
}

int vfs_futimens(struct file *f, int64_t atime, int64_t mtime)
{
	hfs_h h;
	int r = reopen_attr(f, &h);
	if (r) return r;
	r = hfs_set_times(h, atime, mtime);
	hfs_close(h);
	return r;
}

int vfs_truncate(struct fs_ctx *fs, const struct vfs_base *b, const char *path, uint64_t size)
{
	struct file *f;
	int r = vfs_open(fs, b, path, O_WRONLY, 0, &f);
	if (r) return r;
	r = f->ops->truncate ? f->ops->truncate(f, size) : -EINVAL;
	file_put(f);
	return r;
}

int vfs_chdir(struct fs_ctx *fs, const struct vfs_base *b, const char *path)
{
	struct vfs_loc loc;
	struct kstat st;
	int r = walk(fs, b, path, W_MUSTDIR, &loc);
	if (r) return r;
	r = stat_abs(fs, loc.abs, &st, NULL);
	if (r) return r;
	r = may(fs, &st, X_OK, 1);
	if (r) return r;
	strcpy(fs->cwd, loc.abs);
	return 0;
}

int vfs_fchdir(struct fs_ctx *fs, struct file *f)
{
	if (f->kind != FILE_KIND_HOSTDIR && !(f->kind == FILE_KIND_PROC && f->path[0])) return -ENOTDIR;
	strcpy(fs->cwd, f->path);
	return 0;
}

int64_t vfs_getcwd(struct fs_ctx *fs, char *buf, size_t cap)
{
	size_t n = strlen(fs->cwd);
	if (n + 1 > cap) return -ERANGE;
	memcpy(buf, fs->cwd, n + 1);
	return (int64_t)n + 1;
}
