/*
 * hostfs.h -- the host file store beneath the VFS, in Linux-neutral terms.
 *
 * The VFS (0011 § 8) owns path resolution, permissions, the mount table,
 * symlink semantics and the descriptor layer; NTFS owns the bytes and the
 * names; and the metadata Linux needs that NTFS does not carry lives in the
 * extended attributes and reparse tags WSL defined.  This header is the line
 * between those two: every call names a host object by an opaque handle and a
 * single path component, speaks Linux modes, uids and nanosecond times, and
 * returns a negative Linux errno.  Only hostfs_nt.c, beneath the substrate
 * line by declaration, knows what the handle is or which NT call answers.
 *
 * A component is one name, UTF-8, with no slash in it.  The VFS walks a path
 * one component at a time from a directory handle, which is what makes a
 * relative open race-free and lets the VFS apply Linux's rules for symlinks,
 * `..` and permissions between components rather than NTFS's.  The one
 * exception is hfs_open_root, which takes a whole host path to seat a mount.
 *
 * Symlinks are the VFS's to follow.  An open that meets an LX symlink returns
 * HFS_REPARSE with the tag and a handle to the link object itself, so the
 * caller can read the target and splice it in; a Windows symlink or junction
 * is followed by NTFS inside the call, invisibly, because Linux has no
 * reading of it that the VFS could apply.
 */
#ifndef CORE_HOSTFS_H
#define CORE_HOSTFS_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t hfs_h;			/* an open host object; 0 is none */

/* What a stat returns.  Times are nanoseconds since the Unix epoch.  mode is
 * the full Linux st_mode (type bits included), synthesised by the VFS when the
 * LX attributes are absent (hfs_stat reports which were present in lxflags). */
struct hfs_stat {
	uint64_t ino;			/* NTFS file reference number */
	uint64_t dev;			/* volume serial */
	uint64_t size;
	uint64_t blocks;		/* 512-byte units of allocation */
	uint32_t nlink;
	uint32_t lxflags;		/* HFS_LX_* below: which attributes exist */
	uint32_t mode;			/* valid when HFS_LX_MODE is set */
	uint32_t uid, gid;		/* valid when HFS_LX_UID / GID are set */
	uint32_t rdev_major, rdev_minor;	/* valid when HFS_LX_DEV is set */
	int64_t atime, mtime, ctime, btime;
	uint32_t reparse_tag;		/* 0, or the tag on the object */
	uint32_t kind;			/* HFS_KIND_* */
};

#define HFS_LX_UID	0x1
#define HFS_LX_GID	0x2
#define HFS_LX_MODE	0x4
#define HFS_LX_DEV	0x8
#define HFS_LX_CASE	0x10		/* directory is case-sensitive */

/* What NTFS says the object is, before any LX attribute is read. */
#define HFS_KIND_FILE	1
#define HFS_KIND_DIR	2
#define HFS_KIND_LXLINK	3		/* LX symlink reparse point */
#define HFS_KIND_FIFO	4		/* LX FIFO reparse point */
#define HFS_KIND_CHR	5
#define HFS_KIND_BLK	6
#define HFS_KIND_SOCK	7		/* AF_UNIX reparse point */
#define HFS_KIND_OTHER	8		/* a Windows reparse point of another tag */

/* Open flags.  READ and WRITE are the access asked for; DIR insists on a
 * directory and NODIR refuses one; CREATE, EXCL and TRUNC are open(2)'s.
 * REPARSE opens the object itself when it is a reparse point rather than
 * following it, which is what lstat and readlink need. */
#define HFS_O_READ	0x001
#define HFS_O_WRITE	0x002
#define HFS_O_DIR	0x004
#define HFS_O_NODIR	0x008
#define HFS_O_CREATE	0x010
#define HFS_O_EXCL	0x020
#define HFS_O_TRUNC	0x040
#define HFS_O_REPARSE	0x080
#define HFS_O_DELETE	0x100		/* ask for delete access (unlink, rename) */
#define HFS_O_ATTR	0x200		/* attributes only: stat, chmod, utimens */

/* hfs_openat's positive return: the component is an LX reparse point and *out
 * holds the object itself (opened as with HFS_O_REPARSE), st says which kind.
 * A caller that asked for HFS_O_REPARSE gets 0 instead, with the same handle. */
#define HFS_REPARSE	1

/*
 * Seat a mount: open the host directory at path (UTF-8, in the host's own
 * spelling, e.g. "C:\\lk\\root") and return its handle.  0 or -errno.
 */
int hfs_open_root(const char *path, hfs_h *out);

/*
 * Open one component under dir.  On create, mode is the Linux mode to record
 * in the LX attributes at creation (uid and gid as given), so a new file is
 * born with its metadata rather than acquiring it in a second call.  Returns
 * 0 with *out set, HFS_REPARSE as above, or -errno.  st, if not NULL, receives
 * the object's stat as opened, which saves the caller a second query.
 */
int hfs_openat(hfs_h dir, const char *name, unsigned flags, uint32_t mode,
	       uint32_t uid, uint32_t gid, hfs_h *out, struct hfs_stat *st);

void hfs_close(hfs_h h);

/* Open the object h refers to again, with its own access (flags as for
 * hfs_openat, without CREATE); what a mount root or an fchdir needs. */
int hfs_reopen(hfs_h h, unsigned flags, hfs_h *out, struct hfs_stat *st);

/* Stat an open object. */
int hfs_stat(hfs_h h, struct hfs_stat *st);

/* Stat one component under dir without keeping it open.  follow says whether
 * an LX symlink is reported as itself (0) -- the caller follows -- which is
 * the only reading this call has: a Windows link is always followed. */
int hfs_statat(hfs_h dir, const char *name, struct hfs_stat *st);

/* Positional read and write; the VFS keeps the offset.  Return the count, 0 at
 * end of file for a read, or -errno.  A write with append set lands at the end
 * whatever off says. */
int64_t hfs_pread(hfs_h h, void *buf, size_t len, uint64_t off);
int64_t hfs_pwrite(hfs_h h, const void *buf, size_t len, uint64_t off, int append);

int hfs_truncate(hfs_h h, uint64_t size);
int hfs_fsync(hfs_h h);

/* Set the LX attributes named in which (HFS_LX_UID, GID, MODE, DEV). */
int hfs_set_lx(hfs_h h, uint32_t which, uint32_t uid, uint32_t gid,
	       uint32_t mode, uint32_t major, uint32_t minor);

/* Set times; HFS_TIME_OMIT leaves one alone, HFS_TIME_NOW takes the host's
 * clock.  ctime is the host's to advance. */
#define HFS_TIME_OMIT	((int64_t)-1)
#define HFS_TIME_NOW	((int64_t)-2)
int hfs_set_times(hfs_h h, int64_t atime, int64_t mtime);

/* Directories.  mkdir creates with the LX mode; case makes an existing
 * directory case-sensitive, which the root's installer does per directory. */
int hfs_mkdir(hfs_h dir, const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
	      int case_sensitive);
int hfs_set_case_sensitive(hfs_h dirh);

/* Remove one component: a file or an empty directory.  POSIX semantics where
 * the volume has them, so an open file loses its name and keeps its bytes;
 * -EBUSY where a Windows program holds the file without share-delete,
 * -ENOTEMPTY for a directory with children, -EISDIR / -ENOTDIR on a kind the
 * caller did not ask for (rmdir says is_dir). */
int hfs_unlink(hfs_h dir, const char *name, int is_dir);

/* Rename old under olddir to new under newdir, replacing an existing target
 * (a file over a file, an empty directory over an empty directory) unless
 * noreplace, in which case an existing target is -EEXIST. */
int hfs_rename(hfs_h olddir, const char *oldname, hfs_h newdir,
	       const char *newname, int noreplace);

/* Hard link the open object h as name under dir. */
int hfs_link(hfs_h h, hfs_h dir, const char *name);

/* Create an LX symlink named name under dir with the UTF-8 target. */
int hfs_symlink(hfs_h dir, const char *name, const char *target,
		uint32_t uid, uint32_t gid);

/* Read an LX symlink's target into buf (no terminator); return its length,
 * which may exceed cap, in which case cap bytes were copied. */
int64_t hfs_readlink(hfs_h h, char *buf, size_t cap);

/* Create a special file: kind is HFS_KIND_FIFO, CHR, BLK or SOCK. */
int hfs_mknod(hfs_h dir, const char *name, uint32_t kind, uint32_t mode,
	      uint32_t uid, uint32_t gid, uint32_t major, uint32_t minor);

/*
 * Directory reading.  A reader is a cursor over an open directory handle
 * with the host's batch buffered inside it, so the VFS can ask for any number
 * of entries at a time and nothing is lost between calls.  hfs_dir_read fills
 * up to cap entries from the cursor's position (restart rewinds first) and
 * returns the count, 0 at the end, or -errno.  Names are UTF-8 with the WSL
 * escape undone; "." and ".." are not returned and the VFS synthesises them.
 * The reader does not own the handle; close it after the reader.
 */
struct hfs_dirent {
	uint64_t ino;
	uint32_t kind;			/* HFS_KIND_* */
	char name[256];
};
struct hfs_dir;
struct hfs_dir *hfs_dir_open(hfs_h h);
int hfs_dir_read(struct hfs_dir *d, struct hfs_dirent *ents, size_t cap, int restart);
void hfs_dir_close(struct hfs_dir *d);

/* Whether the volume behind h supports the POSIX delete and rename
 * semantics, the LX attributes, and case-sensitive directories: NTFS on the
 * builds the design floor names.  0 or 1; -errno if the question fails. */
int hfs_volume_is_lx_capable(hfs_h h);

/* The volume serial behind h, which the VFS reports as st_dev per mount. */
uint64_t hfs_volume_serial(hfs_h h);

/* The host's clock, nanoseconds since the Unix epoch. */
int64_t hfs_now(void);

#endif /* CORE_HOSTFS_H */
