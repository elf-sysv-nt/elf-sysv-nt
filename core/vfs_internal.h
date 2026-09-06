/*
 * vfs_internal.h -- what vfs.c and the in-kernel file systems share.
 */
#ifndef CORE_VFS_INTERNAL_H
#define CORE_VFS_INTERNAL_H

#include "vfs.h"

/* A resolved path: the mount it is on, its canonical absolute path, and,
 * for a resolution that stopped at the parent, the last component. */
struct vfs_loc {
	struct mount *m;
	char abs[LX_PATH_MAX];		/* the object, or the parent when last[0] */
	char last[LX_NAME_MAX + 1];
	const char *rel;		/* abs relative to m->point, no leading slash ("" at the root) */
};

/* Path arithmetic over canonical absolute paths. */
void path_parent(char *abs);				/* "/a/b" -> "/a"; "/" stays */
int path_join(char *out, size_t cap, const char *dir, const char *name);
const char *path_rel(const struct mount *m, const char *abs);

/* The in-kernel file systems answer these for a path under their mount. */
int procfs_stat(struct fs_ctx *fs, const char *rel, struct kstat *st);
int procfs_open(struct fs_ctx *fs, const char *rel, unsigned flags, struct file **out);
int64_t procfs_readlink(struct fs_ctx *fs, const char *rel, char *buf, size_t cap);
int devfs_stat(struct fs_ctx *fs, const char *rel, struct kstat *st);
int devfs_open(struct fs_ctx *fs, const char *rel, unsigned flags, struct file **out);
int64_t devfs_readlink(struct fs_ctx *fs, const char *rel, char *buf, size_t cap);

/* Helpers the synthetics use to answer stat and getdents. */
void kstat_synthetic(struct kstat *st, uint64_t dev, uint64_t ino, uint32_t mode, uint64_t size);
int64_t dirent_pack(void *buf, size_t cap, size_t *used, uint64_t ino, int64_t off,
		    uint8_t type, const char *name);

const char *vfs_exe_path(void);

#endif /* CORE_VFS_INTERNAL_H */
