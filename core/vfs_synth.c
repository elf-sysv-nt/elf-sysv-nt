/*
 * vfs_synth.c -- the file kinds that have no host object behind them: the
 * /proc and /dev file systems, the pipe, and the console the process
 * inherited (0011 § 8 and § 9).
 *
 * /proc/self is generated at open: maps from the VMA tree, exe and cwd as
 * symlinks, fd/ as a directory of symlinks to what each descriptor was opened
 * at.  A generated file is a snapshot read from a buffer, which is what a
 * reader depends on.  /dev holds the four memory devices, random, and the
 * standard-stream symlinks WSL provides.  The pipe is a ring in kernel
 * memory; 0011 § 9's ring in a shared section arrives with fork, and until
 * then a read that would block on an empty ring with a writer alive returns
 * EAGAIN, since no other thread of this process could fill it -- the one
 * divergence this file admits, and it goes with phase 3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"
#include "vfs_internal.h"
#include "vma.h"
#include "host.h"
#include "lxerrno.h"

/* ---- generated files: read from a snapshot ---------------------------------- */

static int64_t snap_read(struct file *f, void *buf, size_t len)
{
	const char *s = f->priv;
	size_t total = (size_t)f->priv_i, n;
	if (!s || f->pos >= total) return 0;
	n = total - (size_t)f->pos;
	if (n > len) n = len;
	memcpy(buf, s + f->pos, n);
	f->pos += n;
	return (int64_t)n;
}

static int64_t snap_pread(struct file *f, void *buf, size_t len, uint64_t off)
{
	const char *s = f->priv;
	size_t total = (size_t)f->priv_i, n;
	if (!s || off >= total) return 0;
	n = total - (size_t)off;
	if (n > len) n = len;
	memcpy(buf, s + off, n);
	return (int64_t)n;
}

static int64_t snap_write(struct file *f, const void *buf, size_t len)
{
	(void)f; (void)buf; (void)len;
	return -EINVAL;		/* a generated file has no write, as on Linux */
}

static int64_t snap_lseek(struct file *f, int64_t off, int whence)
{
	int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)f->pos
		     : whence == SEEK_END ? f->priv_i : -1;
	if (base < 0 || base + off < 0) return -EINVAL;
	f->pos = (uint64_t)(base + off);
	return (int64_t)f->pos;
}

static void snap_release(struct file *f)
{
	free(f->priv);
	f->priv = NULL;
}

/* ---- /proc ------------------------------------------------------------------- */

#define PROC_DEV	0x4
#define PROC_INO(x)	(0x10000 + (x))

static char exe_path[LX_PATH_MAX] = "/";

void vfs_set_exe_path(const char *linux_path)
{
	strncpy(exe_path, linux_path, sizeof exe_path - 1);
}

const char *vfs_exe_path(void)
{
	return exe_path;
}

/* The tree, flattened: each entry knows its parent by name prefix. */
enum { P_ROOT, P_SELF, P_MAPS, P_EXE, P_CWD, P_FD, P_FDN, P_SELFLINK, P_NONE };

static int proc_classify(const char *rel, int *fdn)
{
	char pid[16];
	const char *p;
	*fdn = -1;
	if (!*rel) return P_ROOT;
	snprintf(pid, sizeof pid, "%d", 1);
	if (strcmp(rel, "self") == 0) return P_SELFLINK;
	if (strncmp(rel, "self", 4) == 0 && rel[4] == '/') p = rel + 4;
	else if (strncmp(rel, pid, strlen(pid)) == 0 && (rel[strlen(pid)] == 0 || rel[strlen(pid)] == '/')) p = rel + strlen(pid);
	else return P_NONE;
	if (!*p) return P_SELF;
	p++;
	if (!strcmp(p, "maps")) return P_MAPS;
	if (!strcmp(p, "exe")) return P_EXE;
	if (!strcmp(p, "cwd")) return P_CWD;
	if (!strcmp(p, "fd")) return P_FD;
	if (strncmp(p, "fd/", 3) == 0) {
		char *end;
		long n = strtol(p + 3, &end, 10);
		if (*end || end == p + 3 || n < 0 || n >= NR_OPEN_DEFAULT) return P_NONE;
		if (!fd_get(vfs_current_fdtable(), (int)n)) return P_NONE;
		*fdn = (int)n;
		return P_FDN;
	}
	return P_NONE;
}

static uint32_t proc_mode(int cls)
{
	switch (cls) {
	case P_ROOT: case P_SELF: return S_IFDIR | 0555;
	case P_FD: return S_IFDIR | 0500;
	case P_MAPS: return S_IFREG | 0444;
	case P_EXE: case P_CWD: case P_FDN: case P_SELFLINK: return S_IFLNK | 0777;
	default: return 0;
	}
}

int procfs_stat(struct fs_ctx *fs, const char *rel, struct kstat *st)
{
	int fdn, cls = proc_classify(rel, &fdn);
	(void)fs;
	if (cls == P_NONE) return -ENOENT;
	kstat_synthetic(st, PROC_DEV, PROC_INO(cls * 4096 + (fdn >= 0 ? fdn : 0)), proc_mode(cls), 0);
	if (cls == P_ROOT || cls == P_SELF || cls == P_FD) st->nlink = 2;
	return 0;
}

int64_t procfs_readlink(struct fs_ctx *fs, const char *rel, char *buf, size_t cap)
{
	int fdn, cls = proc_classify(rel, &fdn);
	const char *t;
	char tmp[LX_PATH_MAX];
	size_t n;
	switch (cls) {
	case P_SELFLINK: snprintf(tmp, sizeof tmp, "%d", vfs_current_fs() ? 1 : 1); t = tmp; break;
	case P_EXE: t = exe_path; break;
	case P_CWD: t = fs->cwd; break;
	case P_FDN: {
		struct file *f = fd_get(vfs_current_fdtable(), fdn);
		if (!f) return -ENOENT;
		if (f->kind == FILE_KIND_PIPE) { snprintf(tmp, sizeof tmp, "pipe:[%lld]", (long long)f->priv_i); t = tmp; }
		else if (f->kind == FILE_KIND_CONSOLE) t = "/dev/console";
		else t = f->path;
		break;
	}
	case P_NONE: return -ENOENT;
	default: return -EINVAL;
	}
	n = strlen(t);
	memcpy(buf, t, n < cap ? n : cap);
	return (int64_t)n;
}

static const struct file_ops snap_ops = {
	snap_read, snap_write, snap_pread, NULL, snap_lseek, NULL, NULL, NULL, NULL, snap_release,
};

static int proc_file_stat(struct file *f, struct kstat *st)
{
	kstat_synthetic(st, PROC_DEV, PROC_INO((uint64_t)f->priv_i + 7), S_IFREG | 0444, 0);
	return 0;
}

/* a /proc directory: the entries are a static list rendered at open */
struct proc_dir {
	int cls;
	int n;
	char names[NR_OPEN_DEFAULT + 4][16];
	uint8_t types[NR_OPEN_DEFAULT + 4];
};

static int64_t proc_getdents(struct file *f, void *buf, size_t len)
{
	struct proc_dir *d = f->priv;
	size_t used = 0;
	if (!d) return -EINVAL;
	while (f->pos < (uint64_t)d->n + 2) {
		const char *name;
		uint8_t type;
		if (f->pos == 0) { name = "."; type = DT_DIR; }
		else if (f->pos == 1) { name = ".."; type = DT_DIR; }
		else { name = d->names[f->pos - 2]; type = d->types[f->pos - 2]; }
		if (dirent_pack(buf, len, &used, PROC_INO(d->cls * 4096 + f->pos), (int64_t)f->pos + 1, type, name) < 0)
			return used ? (int64_t)used : -EINVAL;
		f->pos++;
	}
	return (int64_t)used;
}

static int64_t proc_dir_lseek(struct file *f, int64_t off, int whence)
{
	if (whence == SEEK_SET && off == 0) { f->pos = 0; return 0; }
	if (whence == SEEK_CUR && off == 0) return (int64_t)f->pos;
	return -EINVAL;
}

static int64_t eisdir_read(struct file *f, void *buf, size_t len) { (void)f; (void)buf; (void)len; return -EISDIR; }
static int64_t eisdir_write(struct file *f, const void *buf, size_t len) { (void)f; (void)buf; (void)len; return -EISDIR; }

static int proc_dir_stat(struct file *f, struct kstat *st)
{
	struct proc_dir *d = f->priv;
	kstat_synthetic(st, PROC_DEV, PROC_INO(d ? d->cls * 4096 : 0), proc_mode(d ? d->cls : P_ROOT), 0);
	st->nlink = 2;
	return 0;
}

static const struct file_ops proc_dir_ops = {
	eisdir_read, eisdir_write, NULL, NULL, proc_dir_lseek, proc_dir_stat, proc_getdents, NULL, NULL, snap_release,
};

static const struct file_ops proc_file_ops = {
	snap_read, snap_write, snap_pread, NULL, snap_lseek, proc_file_stat, NULL, NULL, NULL, snap_release,
};

int procfs_open(struct fs_ctx *fs, const char *rel, unsigned flags, struct file **out)
{
	int fdn, cls = proc_classify(rel, &fdn);
	struct file *f;
	(void)fs;
	*out = NULL;
	if (cls == P_NONE) return -ENOENT;
	if (cls == P_EXE || cls == P_CWD || cls == P_FDN || cls == P_SELFLINK) {
		if (flags & O_PATH) {
			f = file_new(&snap_ops, FILE_KIND_PROC, flags);
			if (!f) return -ENOMEM;
			snprintf(f->path, sizeof f->path, "/proc/%s", rel);
			*out = f;
			return 0;
		}
		return -ELOOP;	/* the walk follows these; reaching here means O_NOFOLLOW */
	}
	if (cls == P_ROOT || cls == P_SELF || cls == P_FD) {
		struct proc_dir *d;
		if ((flags & O_ACCMODE) != O_RDONLY) return -EISDIR;
		d = calloc(1, sizeof *d);
		if (!d) return -ENOMEM;
		d->cls = cls;
		if (cls == P_ROOT) { strcpy(d->names[0], "self"); d->types[0] = DT_LNK; strcpy(d->names[1], "1"); d->types[1] = DT_DIR; d->n = 2; }
		else if (cls == P_SELF) {
			const char *names[] = { "cwd", "exe", "fd", "maps" };
			uint8_t types[] = { DT_LNK, DT_LNK, DT_DIR, DT_REG };
			int i;
			for (i = 0; i < 4; i++) { strcpy(d->names[i], names[i]); d->types[i] = types[i]; }
			d->n = 4;
		} else {
			int i;
			for (i = 0; i < NR_OPEN_DEFAULT; i++) {
				if (fd_get(vfs_current_fdtable(), i)) {
					snprintf(d->names[d->n], 16, "%d", i);
					d->types[d->n] = DT_LNK;
					d->n++;
				}
			}
		}
		f = file_new(&proc_dir_ops, FILE_KIND_PROC, flags);
		if (!f) { free(d); return -ENOMEM; }
		f->priv = d;
		snprintf(f->path, sizeof f->path, "/proc%s%s", *rel ? "/" : "", rel);
		*out = f;
		return 0;
	}
	/* maps: root opens it for writing too, and the write then fails */
	{
		char *s = malloc(65536);
		size_t n;
		if (!s) return -ENOMEM;
		n = vma_render_maps(s, 65536);
		f = file_new(&proc_file_ops, FILE_KIND_PROC, flags);
		if (!f) { free(s); return -ENOMEM; }
		f->priv = s;
		f->priv_i = (int64_t)n;
		snprintf(f->path, sizeof f->path, "/proc/%s", rel);
		*out = f;
		return 0;
	}
}

/* ---- /dev ------------------------------------------------------------------- */

#define DEV_DEV		0x5

struct devnode { const char *name; uint32_t mode; uint32_t major, minor; const char *link; };

static const struct devnode devnodes[] = {
	{ "null", S_IFCHR | 0666, 1, 3, NULL },
	{ "zero", S_IFCHR | 0666, 1, 5, NULL },
	{ "full", S_IFCHR | 0666, 1, 7, NULL },
	{ "random", S_IFCHR | 0666, 1, 8, NULL },
	{ "urandom", S_IFCHR | 0666, 1, 9, NULL },
	{ "tty", S_IFCHR | 0666, 5, 0, NULL },
	{ "console", S_IFCHR | 0600, 5, 1, NULL },
	{ "stdin", S_IFLNK | 0777, 0, 0, "/proc/self/fd/0" },
	{ "stdout", S_IFLNK | 0777, 0, 0, "/proc/self/fd/1" },
	{ "stderr", S_IFLNK | 0777, 0, 0, "/proc/self/fd/2" },
	{ "fd", S_IFLNK | 0777, 0, 0, "/proc/self/fd" },
};
#define NDEV (sizeof devnodes / sizeof devnodes[0])

static const struct devnode *dev_find(const char *rel)
{
	size_t i;
	for (i = 0; i < NDEV; i++) if (!strcmp(devnodes[i].name, rel)) return &devnodes[i];
	return NULL;
}

int devfs_stat(struct fs_ctx *fs, const char *rel, struct kstat *st)
{
	const struct devnode *d;
	(void)fs;
	if (!*rel) { kstat_synthetic(st, DEV_DEV, 1, S_IFDIR | 0755, 0); st->nlink = 2; return 0; }
	d = dev_find(rel);
	if (!d) return -ENOENT;
	kstat_synthetic(st, DEV_DEV, 2 + (uint64_t)(d - devnodes), d->mode, d->link ? strlen(d->link) : 0);
	st->rdev_major = d->major;
	st->rdev_minor = d->minor;
	return 0;
}

int64_t devfs_readlink(struct fs_ctx *fs, const char *rel, char *buf, size_t cap)
{
	const struct devnode *d = dev_find(rel);
	size_t n;
	(void)fs;
	if (!d) return -ENOENT;
	if (!d->link) return -EINVAL;
	n = strlen(d->link);
	memcpy(buf, d->link, n < cap ? n : cap);
	return (int64_t)n;
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t rng_next(void)
{
	uint64_t z = (rng_state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static int64_t dev_read(struct file *f, void *buf, size_t len)
{
	switch ((int)f->priv_i) {
	case 3: return 0;			/* null */
	case 5: case 7: memset(buf, 0, len); return (int64_t)len;	/* zero, full */
	case 8: case 9: {			/* random, urandom: not a CSPRNG; the seed is the clock */
		size_t i;
		unsigned char *p = buf;
		for (i = 0; i < len; i++) {
			if ((i & 7) == 0) { uint64_t v = rng_next(); memcpy(p + i, &v, len - i < 8 ? len - i : 8); }
		}
		return (int64_t)len;
	}
	case 0: case 1:				/* tty, console: the host console */
		return host_console_read(0, buf, len);
	default: return -ENXIO;
	}
}

static int64_t dev_write(struct file *f, const void *buf, size_t len)
{
	switch ((int)f->priv_i) {
	case 3: case 5: case 8: case 9: return (int64_t)len;
	case 7: return -ENOSPC;
	case 0: case 1: return host_console_write(1, buf, len);
	default: return -ENXIO;
	}
}

static int64_t dev_lseek(struct file *f, int64_t off, int whence)
{
	(void)f; (void)off; (void)whence;
	return 0;
}

static int dev_stat(struct file *f, struct kstat *st)
{
	const struct devnode *d = f->priv;
	kstat_synthetic(st, DEV_DEV, 2 + (uint64_t)(d - devnodes), d->mode, 0);
	st->rdev_major = d->major;
	st->rdev_minor = d->minor;
	return 0;
}

static const struct file_ops dev_ops = {
	dev_read, dev_write, NULL, NULL, dev_lseek, dev_stat, NULL, NULL, NULL, NULL,
};

static int64_t devdir_getdents(struct file *f, void *buf, size_t len)
{
	size_t used = 0;
	while (f->pos < NDEV + 2) {
		const char *name;
		uint8_t type;
		if (f->pos == 0) { name = "."; type = DT_DIR; }
		else if (f->pos == 1) { name = ".."; type = DT_DIR; }
		else {
			const struct devnode *d = &devnodes[f->pos - 2];
			name = d->name;
			type = d->link ? DT_LNK : DT_CHR;
		}
		if (dirent_pack(buf, len, &used, 2 + f->pos, (int64_t)f->pos + 1, type, name) < 0)
			return used ? (int64_t)used : -EINVAL;
		f->pos++;
	}
	return (int64_t)used;
}

static int devdir_stat(struct file *f, struct kstat *st)
{
	(void)f;
	kstat_synthetic(st, DEV_DEV, 1, S_IFDIR | 0755, 0);
	st->nlink = 2;
	return 0;
}

static const struct file_ops devdir_ops = {
	eisdir_read, eisdir_write, NULL, NULL, proc_dir_lseek, devdir_stat, devdir_getdents, NULL, NULL, NULL,
};

int devfs_open(struct fs_ctx *fs, const char *rel, unsigned flags, struct file **out)
{
	const struct devnode *d;
	struct file *f;
	(void)fs;
	*out = NULL;
	if (!*rel) {
		if ((flags & O_ACCMODE) != O_RDONLY) return -EISDIR;
		f = file_new(&devdir_ops, FILE_KIND_DEV, flags);
		if (!f) return -ENOMEM;
		strcpy(f->path, "/dev");
		*out = f;
		return 0;
	}
	d = dev_find(rel);
	if (!d) return -ENOENT;
	if (d->link) return (flags & O_PATH) ? -ENXIO : -ELOOP;
	if (flags & O_DIRECTORY) return -ENOTDIR;
	f = file_new(&dev_ops, FILE_KIND_DEV, flags);
	if (!f) return -ENOMEM;
	f->priv = (void *)d;
	f->priv_i = d->minor + (d->major == 5 ? 0 : 0);
	if (d->major == 5) f->priv_i = d->minor;	/* tty 0, console 1 */
	snprintf(f->path, sizeof f->path, "/dev/%s", rel);
	*out = f;
	return 0;
}

/* ---- the pipe ---------------------------------------------------------------- */

#define PIPE_DEV	0xc
#define RING		65536

struct pipe_ring {
	unsigned char buf[RING];
	size_t head, tail, count;
	int readers, writers;
	uint64_t ino;
};

static uint64_t next_pipe_ino = 1;

static int64_t pipe_read(struct file *f, void *buf, size_t len)
{
	struct pipe_ring *p = f->priv;
	size_t n = 0;
	unsigned char *out = buf;
	if (p->count == 0) {
		if (p->writers == 0) return 0;
		return -EAGAIN;		/* would block; see the file's head */
	}
	while (n < len && p->count > 0) {
		out[n++] = p->buf[p->head];
		p->head = (p->head + 1) % RING;
		p->count--;
	}
	return (int64_t)n;
}

static int64_t pipe_write(struct file *f, const void *buf, size_t len)
{
	struct pipe_ring *p = f->priv;
	const unsigned char *in = buf;
	size_t n = 0;
	if (p->readers == 0) return -EPIPE;
	if (len <= LX_PIPE_BUF && RING - p->count < len) return -EAGAIN;
	if (RING - p->count == 0) return -EAGAIN;
	while (n < len && p->count < RING) {
		p->buf[p->tail] = in[n++];
		p->tail = (p->tail + 1) % RING;
		p->count++;
	}
	return (int64_t)n;
}

static int64_t pipe_lseek(struct file *f, int64_t off, int whence)
{
	(void)f; (void)off; (void)whence;
	return -ESPIPE;
}

static int pipe_stat(struct file *f, struct kstat *st)
{
	struct pipe_ring *p = f->priv;
	kstat_synthetic(st, PIPE_DEV, p->ino, S_IFIFO | 0600, 0);
	return 0;
}

static void pipe_release(struct file *f)
{
	struct pipe_ring *p = f->priv;
	if ((f->flags & O_ACCMODE) == O_RDONLY) p->readers--; else p->writers--;
	if (p->readers == 0 && p->writers == 0) free(p);
}

static const struct file_ops pipe_ops = {
	pipe_read, pipe_write, NULL, NULL, pipe_lseek, pipe_stat, NULL, NULL, NULL, pipe_release,
};

int vfs_pipe(struct file **rd, struct file **wr, unsigned flags)
{
	struct pipe_ring *p = calloc(1, sizeof *p);
	if (!p) return -ENOMEM;
	p->ino = next_pipe_ino++;
	*rd = file_new(&pipe_ops, FILE_KIND_PIPE, O_RDONLY | (flags & O_NONBLOCK));
	*wr = file_new(&pipe_ops, FILE_KIND_PIPE, O_WRONLY | (flags & O_NONBLOCK));
	if (!*rd || !*wr) { free(*rd); free(*wr); free(p); return -ENOMEM; }
	(*rd)->priv = p; (*rd)->priv_i = (int64_t)p->ino; p->readers = 1;
	(*wr)->priv = p; (*wr)->priv_i = (int64_t)p->ino; p->writers = 1;
	snprintf((*rd)->path, sizeof (*rd)->path, "pipe:[%llu]", (unsigned long long)p->ino);
	strcpy((*wr)->path, (*rd)->path);
	return 0;
}

/* ---- the console ----------------------------------------------------------------- */

static int64_t con_read(struct file *f, void *buf, size_t len)
{
	return host_console_read((int)f->priv_i, buf, len);
}

static int64_t con_write(struct file *f, const void *buf, size_t len)
{
	return host_console_write((int)f->priv_i, buf, len);
}

static int con_stat(struct file *f, struct kstat *st)
{
	(void)f;
	kstat_synthetic(st, DEV_DEV, 9, S_IFCHR | 0620, 0);
	st->rdev_major = 136;
	st->rdev_minor = 0;
	return 0;
}

static const struct file_ops con_ops = {
	con_read, con_write, NULL, NULL, pipe_lseek, con_stat, NULL, NULL, NULL, NULL,
};

struct file *vfs_console_file(int hostfd)
{
	struct file *f = file_new(&con_ops, FILE_KIND_CONSOLE, hostfd == 0 ? O_RDONLY : O_WRONLY);
	if (!f) return NULL;
	f->priv_i = hostfd;
	strcpy(f->path, "/dev/console");
	return f;
}
