/*
 * lxtypes.h -- the Linux x86-64 UAPI the file syscalls cross: the flag and
 * mode constants, the structures a program copies out of a call, and the
 * syscall numbers, all as el8's 4.18 headers define them (0011 § 1: the
 * constants are Linux's by construction).  Nothing here is a host type.
 */
#ifndef CORE_LXTYPES_H
#define CORE_LXTYPES_H

#include <stdint.h>

/* open(2) flags, octal as the header spells them */
#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_EXCL		00000200
#define O_NOCTTY	00000400
#define O_TRUNC		00001000
#define O_APPEND	00002000
#define O_NONBLOCK	00004000
#define O_DSYNC		00010000
#define O_DIRECT	00040000
#define O_LARGEFILE	00100000
#define O_DIRECTORY	00200000
#define O_NOFOLLOW	00400000
#define O_NOATIME	01000000
#define O_CLOEXEC	02000000
#define O_SYNC		04010000
#define O_PATH		010000000
#define O_TMPFILE	020200000

#define AT_FDCWD		(-100)
#define AT_SYMLINK_NOFOLLOW	0x100
#define AT_REMOVEDIR		0x200
#define AT_SYMLINK_FOLLOW	0x400
#define AT_NO_AUTOMOUNT		0x800
#define AT_EMPTY_PATH		0x1000
#define AT_STATX_SYNC_TYPE	0x6000
#define AT_EACCESS		0x200

#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2
#define SEEK_DATA	3
#define SEEK_HOLE	4

#define F_DUPFD		0
#define F_GETFD		1
#define F_SETFD		2
#define F_GETFL		3
#define F_SETFL		4
#define F_DUPFD_CLOEXEC	1030
#define FD_CLOEXEC	1

#define S_IFMT		0170000
#define S_IFSOCK	0140000
#define S_IFLNK		0120000
#define S_IFREG		0100000
#define S_IFBLK		0060000
#define S_IFDIR		0040000
#define S_IFCHR		0020000
#define S_IFIFO		0010000
#define S_ISUID		0004000
#define S_ISGID		0002000
#define S_ISVTX		0001000

#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)

#define R_OK	4
#define W_OK	2
#define X_OK	1
#define F_OK	0

#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12

#define UTIME_NOW	((1L << 30) - 1L)
#define UTIME_OMIT	((1L << 30) - 2L)

#define RENAME_NOREPLACE	1
#define RENAME_EXCHANGE		2
#define RENAME_WHITEOUT		4

#define LX_PATH_MAX	4096
#define LX_NAME_MAX	255
#define LX_PIPE_BUF	4096

#define STATX_TYPE		0x001U
#define STATX_MODE		0x002U
#define STATX_NLINK		0x004U
#define STATX_UID		0x008U
#define STATX_GID		0x010U
#define STATX_ATIME		0x020U
#define STATX_MTIME		0x040U
#define STATX_CTIME		0x080U
#define STATX_INO		0x100U
#define STATX_SIZE		0x200U
#define STATX_BLOCKS		0x400U
#define STATX_BASIC_STATS	0x7ffU
#define STATX_BTIME		0x800U

struct lx_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

/* struct stat, x86-64: 144 bytes */
struct lx_stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
	struct lx_timespec st_atim;
	struct lx_timespec st_mtim;
	struct lx_timespec st_ctim;
	int64_t __unused[3];
};

struct lx_statx_timestamp {
	int64_t tv_sec;
	uint32_t tv_nsec;
	int32_t __reserved;
};

/* struct statx: 256 bytes */
struct lx_statx {
	uint32_t stx_mask;
	uint32_t stx_blksize;
	uint64_t stx_attributes;
	uint32_t stx_nlink;
	uint32_t stx_uid;
	uint32_t stx_gid;
	uint16_t stx_mode;
	uint16_t __spare0;
	uint64_t stx_ino;
	uint64_t stx_size;
	uint64_t stx_blocks;
	uint64_t stx_attributes_mask;
	struct lx_statx_timestamp stx_atime;
	struct lx_statx_timestamp stx_btime;
	struct lx_statx_timestamp stx_ctime;
	struct lx_statx_timestamp stx_mtime;
	uint32_t stx_rdev_major;
	uint32_t stx_rdev_minor;
	uint32_t stx_dev_major;
	uint32_t stx_dev_minor;
	uint64_t __spare2[14];
};

/* struct linux_dirent64: the header, then the name */
struct lx_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	/* char d_name[]; */
};

struct lx_iovec {
	uint64_t iov_base;		/* a user address, never dereferenced here */
	uint64_t iov_len;
};

/* The syscall numbers this phase answers. */
#define NR_read		0
#define NR_write	1
#define NR_open		2
#define NR_close	3
#define NR_stat		4
#define NR_fstat	5
#define NR_lstat	6
#define NR_lseek	8
#define NR_pread64	17
#define NR_pwrite64	18
#define NR_readv	19
#define NR_writev	20
#define NR_access	21
#define NR_pipe		22
#define NR_dup		32
#define NR_dup2		33
#define NR_getpid	39
#define NR_fcntl	72
#define NR_fsync	74
#define NR_fdatasync	75
#define NR_truncate	76
#define NR_ftruncate	77
#define NR_getcwd	79
#define NR_chdir	80
#define NR_fchdir	81
#define NR_rename	82
#define NR_mkdir	83
#define NR_rmdir	84
#define NR_creat	85
#define NR_link		86
#define NR_unlink	87
#define NR_symlink	88
#define NR_readlink	89
#define NR_chmod	90
#define NR_fchmod	91
#define NR_chown	92
#define NR_fchown	93
#define NR_lchown	94
#define NR_umask	95
#define NR_getuid	102
#define NR_getgid	104
#define NR_geteuid	107
#define NR_getegid	108
#define NR_getdents64	217
#define NR_exit_group	231
#define NR_openat	257
#define NR_mkdirat	258
#define NR_mknodat	259
#define NR_fchownat	260
#define NR_newfstatat	262
#define NR_unlinkat	263
#define NR_renameat	264
#define NR_linkat	265
#define NR_symlinkat	266
#define NR_readlinkat	267
#define NR_fchmodat	268
#define NR_faccessat	269
#define NR_utimensat	280
#define NR_dup3		292
#define NR_pipe2	293
#define NR_renameat2	316
#define NR_statx	332

#endif /* CORE_LXTYPES_H */
