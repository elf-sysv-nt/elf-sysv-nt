/*
 * vfs-trace.c -- criterion 2's program: a scripted sequence of file
 * operations whose trace of results and errnos must read the same under
 * the core as on the el8 reference.
 *
 * Freestanding, no libc: every call is a raw syscall through lksys.h, built
 * once against the gate and once (-DLK_ORACLE) against the `syscall`
 * instruction for the Rocky 8 oracle.  It runs in whatever directory it is
 * started in, which the harness makes empty, and prints one line per
 * observation.  Nothing that legitimately differs between hosts (inode
 * numbers, device numbers, absolute paths, clock readings) is printed as a
 * value; it is printed as a comparison.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "lksys.h"

/* ---- the UAPI this program uses (el8's 4.18 headers) ------------------------- */

#define NR_read 0
#define NR_write 1
#define NR_open 2
#define NR_close 3
#define NR_stat 4
#define NR_fstat 5
#define NR_lstat 6
#define NR_lseek 8
#define NR_pread64 17
#define NR_pwrite64 18
#define NR_writev 20
#define NR_access 21
#define NR_pipe 22
#define NR_dup 32
#define NR_dup2 33
#define NR_fcntl 72
#define NR_truncate 76
#define NR_ftruncate 77
#define NR_getcwd 79
#define NR_chdir 80
#define NR_fchdir 81
#define NR_rename 82
#define NR_mkdir 83
#define NR_rmdir 84
#define NR_link 86
#define NR_unlink 87
#define NR_symlink 88
#define NR_readlink 89
#define NR_chmod 90
#define NR_fchmod 91
#define NR_chown 92
#define NR_lchown 94
#define NR_umask 95
#define NR_getdents64 217
#define NR_openat 257
#define NR_mkdirat 258
#define NR_mknodat 259
#define NR_fchownat 260
#define NR_newfstatat 262
#define NR_unlinkat 263
#define NR_renameat 264
#define NR_linkat 265
#define NR_symlinkat 266
#define NR_readlinkat 267
#define NR_fchmodat 268
#define NR_faccessat 269
#define NR_utimensat 280
#define NR_dup3 292
#define NR_pipe2 293
#define NR_renameat2 316
#define NR_statx 332

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000
#define O_PATH 010000000
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EMPTY_PATH 0x1000
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define S_IFMT 0170000
#define S_IFIFO 0010000
#define UTIME_NOW ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
#define RENAME_NOREPLACE 1
#define STATX_BASIC_STATS 0x7ff
#define STATX_BTIME 0x800

struct timespec { long tv_sec; long tv_nsec; };
struct stat {
	unsigned long st_dev, st_ino, st_nlink;
	unsigned st_mode, st_uid, st_gid, pad0;
	unsigned long st_rdev;
	long st_size, st_blksize, st_blocks;
	struct timespec st_atim, st_mtim, st_ctim;
	long unused[3];
};
struct statx_ts { long tv_sec; unsigned tv_nsec; int pad; };
struct statx {
	unsigned stx_mask, stx_blksize;
	unsigned long stx_attributes;
	unsigned stx_nlink, stx_uid, stx_gid;
	unsigned short stx_mode, pad1;
	unsigned long stx_ino, stx_size, stx_blocks, stx_attributes_mask;
	struct statx_ts stx_atime, stx_btime, stx_ctime, stx_mtime;
	unsigned stx_rdev_major, stx_rdev_minor, stx_dev_major, stx_dev_minor;
	unsigned long spare[14];
};
struct dirent64 { unsigned long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };

/* ---- output ------------------------------------------------------------------- */

static char obuf[8192];
static int olen;

static void flush(void)
{
	int off = 0;
	while (off < olen) {
		long n = lk_syscall3(NR_write, 1, obuf + off, olen - off);
		if (n <= 0) break;
		off += (int)n;
	}
	olen = 0;
}

static void putc_(char c)
{
	if (olen >= (int)sizeof obuf) flush();
	obuf[olen++] = c;
}

static void puts_(const char *s)
{
	while (*s) putc_(*s++);
}

static void putnum(long v, int base)
{
	char tmp[32];
	int i = 0;
	unsigned long u;
	if (v < 0) { putc_('-'); u = (unsigned long)(-v); } else u = (unsigned long)v;
	do { int d = (int)(u % (unsigned long)base); tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned long)base; } while (u);
	while (i) putc_(tmp[--i]);
}

static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		if (*fmt != '%') { putc_(*fmt); continue; }
		fmt++;
		switch (*fmt) {
		case 's': puts_(va_arg(ap, const char *)); break;
		case 'd': putnum(va_arg(ap, long), 10); break;
		case 'o': putc_('0'); putnum(va_arg(ap, long), 8); break;
		case 'x': putnum(va_arg(ap, long), 16); break;
		case 'c': putc_((char)va_arg(ap, int)); break;
		case '%': putc_('%'); break;
		default: putc_('?'); break;
		}
	}
	va_end(ap);
	putc_('\n');
}

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)); }

/* ---- wrappers ------------------------------------------------------------------ */

static long xopen(const char *p, int flags, int mode) { return lk_syscall3(NR_open, p, flags, mode); }
static long xclose(long fd) { return lk_syscall1(NR_close, fd); }
static long xwrite(long fd, const char *s) { return lk_syscall3(NR_write, fd, s, slen(s)); }
static long xread(long fd, char *b, long n) { long r = lk_syscall3(NR_read, fd, b, n); if (r >= 0 && r < n) b[r] = 0; return r; }
static long xstat(const char *p, struct stat *st) { return lk_syscall2(NR_stat, p, st); }
static long xlstat(const char *p, struct stat *st) { return lk_syscall2(NR_lstat, p, st); }
static long xfstat(long fd, struct stat *st) { return lk_syscall2(NR_fstat, fd, st); }

static void show_stat(const char *what, const char *p, int follow)
{
	struct stat st;
	long r = follow ? xstat(p, &st) : xlstat(p, &st);
	if (r) { say("%s %s = %d", what, p, r); return; }
	if ((st.st_mode & S_IFMT) == 0040000)
		say("%s %s = mode %o uid %d gid %d", what, p, (long)st.st_mode, (long)st.st_uid, (long)st.st_gid);
	else
		say("%s %s = mode %o nlink %d size %d uid %d gid %d", what, p, (long)st.st_mode, (long)st.st_nlink, st.st_size, (long)st.st_uid, (long)st.st_gid);
}

static void mkfile(const char *p, const char *content)
{
	long fd = xopen(p, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd >= 0) { if (content) xwrite(fd, content); xclose(fd); }
}

/* ---- the sequence ---------------------------------------------------------------- */

static void t_create(void)
{
	long fd, r;
	char b[64];
	struct stat st;
	say("== create and write");
	say("mkdir d 0750 = %d", lk_syscall2(NR_mkdir, "d", 0750));
	say("mkdir d again = %d", lk_syscall2(NR_mkdir, "d", 0750));
	say("mkdir d/ trailing = %d", lk_syscall2(NR_mkdir, "d/", 0750));
	show_stat("stat", "d", 1);
	fd = xopen("f", O_CREAT | O_EXCL | O_WRONLY, 0640);
	say("open f O_CREAT|O_EXCL = fd>=3: %d", (long)(fd >= 3));
	say("write 11 = %d", xwrite(fd, "hello world"));
	say("open f O_CREAT|O_EXCL again = %d", xopen("f", O_CREAT | O_EXCL | O_WRONLY, 0640));
	show_stat("stat", "f", 1);
	r = xfstat(fd, &st);
	say("fstat f = %d mode %o size %d", r, (long)st.st_mode, st.st_size);
	xclose(fd);
	say("close closed = %d", xclose(fd));
	fd = xopen("f", O_RDONLY, 0);
	say("read 5 = %d %s", xread(fd, b, 5), b);
	say("lseek CUR 0 = %d", lk_syscall3(NR_lseek, fd, 0, SEEK_CUR));
	r = lk_syscall4(NR_pread64, fd, b, 3, 6);
	b[r > 0 ? r : 0] = 0;
	say("pread 3 at 6 = %d %s", r, b);
	say("lseek END 0 = %d", lk_syscall3(NR_lseek, fd, 0, SEEK_END));
	say("read at end = %d", xread(fd, b, 10));
	say("lseek SET 100 = %d", lk_syscall3(NR_lseek, fd, 100, SEEK_SET));
	say("read past end = %d", xread(fd, b, 10));
	say("lseek SET -1 = %d", lk_syscall3(NR_lseek, fd, -1, SEEK_SET));
	say("write on O_RDONLY = %d", xwrite(fd, "x"));
	xclose(fd);
	fd = xopen("f", O_WRONLY | O_APPEND, 0);
	say("append write = %d", xwrite(fd, "!"));
	say("pwrite at 0 with O_APPEND = %d", lk_syscall4(NR_pwrite64, fd, "HELLO", 5, 0));
	xclose(fd);
	fd = xopen("f", O_RDONLY, 0);
	r = xread(fd, b, 60);
	say("content after append = %d %s", r, b);
	say("read on O_WRONLY? no: read on O_RDONLY = %d", (long)(r > 0));
	xclose(fd);
	fd = xopen("f", O_WRONLY | O_TRUNC, 0);
	xstat("f", &st);
	say("open O_TRUNC then size = %d", st.st_size);
	say("write abc = %d", xwrite(fd, "abc"));
	xclose(fd);
	fd = xopen("f", O_RDWR | O_APPEND, 0);
	say("open f O_RDWR|O_APPEND flags = %d", fd >= 0 ? (lk_syscall3(NR_fcntl, fd, F_GETFL, 0) & (O_APPEND | O_RDWR)) : -1);
	xclose(fd);
	fd = xopen("f", 3, 0);
	say("open with accmode 3 ok = %d read = %d write = %d", (long)(fd >= 0), xread(fd, b, 4), xwrite(fd, "x"));
	xclose(fd);
	say("creat via open O_CREAT|O_TRUNC on dir = %d", xopen("d", O_CREAT | O_WRONLY | O_TRUNC, 0644));
	say("open d O_WRONLY = %d", xopen("d", O_WRONLY, 0));
	say("open f O_DIRECTORY = %d", xopen("f", O_RDONLY | O_DIRECTORY, 0));
	say("open nonexist/x = %d", xopen("nonexist/x", O_RDONLY, 0));
	say("open f/x = %d", xopen("f/x", O_RDONLY, 0));
	say("open f/ = %d", xopen("f/", O_RDONLY, 0));
	say("open empty = %d", xopen("", O_RDONLY, 0));
	say("stat f/ = %d", xstat("f/", &st));
	say("stat d/ = %d", xstat("d/", &st));
	say("stat d/./../d = %d", xstat("d/./../d", &st));
	say("stat ../ from cwd = %d", xstat("../", &st));
	{
		char longname[300];
		int i;
		for (i = 0; i < 260; i++) longname[i] = 'n';
		longname[260] = 0;
		say("open 260-char name = %d", xopen(longname, O_CREAT | O_WRONLY, 0644));
	}
	say("umask 077 = %o", lk_syscall1(NR_umask, 077));
	mkfile("masked", NULL);
	show_stat("stat", "masked", 1);
	say("umask 022 = %o", lk_syscall1(NR_umask, 022));
	mkfile("a:b*c?d", "odd");
	show_stat("stat", "a:b*c?d", 1);
	say("stat A:B*C?D (case) = %d", xstat("A:B*C?D", &st));
}

static void t_rename_unlink(void)
{
	long fd, r;
	char b[64];
	struct stat st;
	say("== rename and unlink");
	fd = xopen("f", O_RDONLY, 0);
	mkfile("g", "new");
	say("rename g f over open = %d", lk_syscall2(NR_rename, "g", "f"));
	r = xread(fd, b, 60);
	say("old fd reads = %d %s", r, b);
	xclose(fd);
	show_stat("stat", "f", 1);
	say("stat g = %d", xstat("g", &st));
	fd = xopen("f", O_RDONLY, 0);
	say("unlink f while open = %d", lk_syscall1(NR_unlink, "f"));
	say("stat unlinked = %d", xstat("f", &st));
	r = xread(fd, b, 60);
	say("unlinked fd reads = %d %s", r, b);
	xclose(fd);
	say("open unlinked = %d", xopen("f", O_RDONLY, 0));
	say("unlink absent = %d", lk_syscall1(NR_unlink, "f"));
	say("unlink d = %d", lk_syscall1(NR_unlink, "d"));
	say("rmdir masked = %d", lk_syscall1(NR_rmdir, "masked"));
	say("mkdir d/sub = %d", lk_syscall2(NR_mkdir, "d/sub", 0755));
	say("rmdir d nonempty = %d", lk_syscall1(NR_rmdir, "d"));
	say("rmdir d/sub = %d", lk_syscall1(NR_rmdir, "d/sub"));
	say("rmdir d/x = %d", lk_syscall1(NR_rmdir, "d/x"));
	say("mkdir masked/x = %d", lk_syscall2(NR_mkdir, "masked/x", 0755));
	say("rmdir . = %d", lk_syscall1(NR_rmdir, "."));
	say("mkdir e1 = %d", lk_syscall2(NR_mkdir, "e1", 0755));
	say("mkdir e2 = %d", lk_syscall2(NR_mkdir, "e2", 0755));
	say("rename e1 over empty e2 = %d", lk_syscall2(NR_rename, "e1", "e2"));
	say("stat e1 = %d", xstat("e1", &st));
	say("mkdir d/sub2 = %d", lk_syscall2(NR_mkdir, "d/sub2", 0755));
	say("rename e2 over nonempty d = %d", lk_syscall2(NR_rename, "e2", "d"));
	say("rename masked over dir e2 = %d", lk_syscall2(NR_rename, "masked", "e2"));
	say("rename dir e2 over file masked = %d", lk_syscall2(NR_rename, "e2", "masked"));
	say("rename d into d/sub2/x = %d", lk_syscall2(NR_rename, "d", "d/sub2/x"));
	say("rename absent = %d", lk_syscall2(NR_rename, "absent", "x"));
	say("renameat2 NOREPLACE existing = %d", lk_syscall5(NR_renameat2, AT_FDCWD, "masked", AT_FDCWD, "a:b*c?d", RENAME_NOREPLACE));
	say("renameat2 NOREPLACE new = %d", lk_syscall5(NR_renameat2, AT_FDCWD, "masked", AT_FDCWD, "masked2", RENAME_NOREPLACE));
	say("rename to self = %d", lk_syscall2(NR_rename, "masked2", "masked2"));
	say("rmdir d/sub2 = %d", lk_syscall1(NR_rmdir, "d/sub2"));
	say("rmdir e2 = %d", lk_syscall1(NR_rmdir, "e2"));
}

static void t_links(void)
{
	long r, fd;
	char b[256];
	struct stat a, c;
	say("== symlinks and hard links");
	say("symlink target.txt link = %d", lk_syscall2(NR_symlink, "target.txt", "link"));
	r = lk_syscall3(NR_readlink, "link", b, sizeof b - 1);
	if (r >= 0) b[r] = 0;
	say("readlink link = %d %s", r, b);
	show_stat("lstat", "link", 0);
	say("stat dangling link = %d", xstat("link", &a));
	say("open dangling = %d", xopen("link", O_RDONLY, 0));
	mkfile("target.txt", "T");
	show_stat("stat", "link", 1);
	say("open link O_NOFOLLOW = %d", xopen("link", O_RDONLY | O_NOFOLLOW, 0));
	fd = xopen("link", O_RDONLY | O_NOFOLLOW | O_PATH, 0);
	say("open link O_NOFOLLOW|O_PATH ok = %d", (long)(fd >= 0));
	r = xfstat(fd, &a);
	say("fstat O_PATH link = %d mode %o", r, (long)a.st_mode);
	say("read O_PATH fd = %d", xread(fd, b, 10));
	xclose(fd);
	say("symlink existing name = %d", lk_syscall2(NR_symlink, "x", "link"));
	say("symlink la->lb = %d", lk_syscall2(NR_symlink, "lb", "la"));
	say("symlink lb->la = %d", lk_syscall2(NR_symlink, "la", "lb"));
	say("stat loop = %d", xstat("la", &a));
	say("open loop = %d", xopen("la", O_RDONLY, 0));
	say("unlink la = %d", lk_syscall1(NR_unlink, "la"));
	say("unlink lb = %d", lk_syscall1(NR_unlink, "lb"));
	say("symlink abs = %d", lk_syscall2(NR_symlink, "/nonexistent/x", "labs"));
	r = lk_syscall3(NR_readlink, "labs", b, sizeof b - 1);
	if (r >= 0) b[r] = 0;
	say("readlink labs = %d %s", r, b);
	say("stat labs = %d", xstat("labs", &a));
	say("readlink target.txt (not a link) = %d", lk_syscall3(NR_readlink, "target.txt", b, 10));
	say("readlink short buffer = %d", lk_syscall3(NR_readlink, "link", b, 4));
	say("symlink d/dl -> ../target.txt = %d", lk_syscall2(NR_symlink, "../target.txt", "d/dl"));
	show_stat("stat", "d/dl", 1);
	say("symlink dd -> d = %d", lk_syscall2(NR_symlink, "d", "dd"));
	say("stat dd/dl = %d", xstat("dd/dl", &a));
	say("stat dd/../target.txt = %d", xstat("dd/../target.txt", &a));
	fd = xopen("dd/", O_RDONLY, 0);
	say("open dd/ = %d", fd >= 0 ? 0 : fd);
	xclose(fd);
	say("link target.txt hard.txt = %d", lk_syscall2(NR_link, "target.txt", "hard.txt"));
	xstat("target.txt", &a);
	xstat("hard.txt", &c);
	say("hard link same ino = %d nlink %d", (long)(a.st_ino == c.st_ino), (long)c.st_nlink);
	say("link over existing = %d", lk_syscall2(NR_link, "target.txt", "hard.txt"));
	say("link dir = %d", lk_syscall2(NR_link, "d", "dhard"));
	say("link absent = %d", lk_syscall2(NR_link, "absent", "x"));
	say("link via symlink (no follow) = %d", lk_syscall2(NR_link, "link", "lhard"));
	show_stat("lstat", "lhard", 0);
	say("linkat AT_SYMLINK_FOLLOW = %d", lk_syscall5(NR_linkat, AT_FDCWD, "link", AT_FDCWD, "lhard2", AT_SYMLINK_FOLLOW));
	show_stat("lstat", "lhard2", 0);
	say("unlink hard.txt = %d", lk_syscall1(NR_unlink, "hard.txt"));
	xstat("target.txt", &a);
	say("nlink after unlink = %d", (long)a.st_nlink);
	say("unlink lhard = %d", lk_syscall1(NR_unlink, "lhard"));
	say("unlink lhard2 = %d", lk_syscall1(NR_unlink, "lhard2"));
	say("unlink labs = %d", lk_syscall1(NR_unlink, "labs"));
	say("symlink lnk2 -> newfile = %d", lk_syscall2(NR_symlink, "newfile", "lnk2"));
	fd = xopen("lnk2", O_CREAT | O_WRONLY, 0644);
	say("open lnk2 O_CREAT creates through = %d", (long)(fd >= 0));
	xclose(fd);
	show_stat("stat", "newfile", 1);
	say("open lnk2 O_CREAT|O_EXCL = %d", xopen("lnk2", O_CREAT | O_EXCL | O_WRONLY, 0644));
	say("unlink lnk2 = %d", lk_syscall1(NR_unlink, "lnk2"));
	show_stat("stat", "newfile", 1);
	say("unlink newfile = %d", lk_syscall1(NR_unlink, "newfile"));
}

static void t_attrs(void)
{
	long fd, r;
	struct stat st, lst;
	struct timespec ts[2];
	say("== chmod, chown, times, truncate");
	say("chmod target.txt 0600 = %d", lk_syscall2(NR_chmod, "target.txt", 0600));
	show_stat("stat", "target.txt", 1);
	say("chmod 04755 = %d", lk_syscall2(NR_chmod, "target.txt", 04755));
	show_stat("stat", "target.txt", 1);
	fd = xopen("target.txt", O_RDONLY, 0);
	say("fchmod 0644 = %d", lk_syscall2(NR_fchmod, fd, 0644));
	xclose(fd);
	show_stat("stat", "target.txt", 1);
	say("chmod absent = %d", lk_syscall2(NR_chmod, "absent", 0644));
	say("chmod through link = %d", lk_syscall2(NR_chmod, "link", 0640));
	show_stat("stat", "target.txt", 1);
	show_stat("lstat", "link", 0);
	say("chown 1000 1000 = %d", lk_syscall3(NR_chown, "target.txt", 1000, 1000));
	show_stat("stat", "target.txt", 1);
	say("chown -1 2000 = %d", lk_syscall3(NR_chown, "target.txt", -1, 2000));
	show_stat("stat", "target.txt", 1);
	say("lchown link 500 500 = %d", lk_syscall3(NR_lchown, "link", 500, 500));
	show_stat("lstat", "link", 0);
	show_stat("stat", "link", 1);
	say("chown d 7 7 = %d", lk_syscall3(NR_chown, "d", 7, 7));
	show_stat("stat", "d", 1);
	ts[0].tv_sec = 1600000000; ts[0].tv_nsec = 123456700;
	ts[1].tv_sec = 1700000000; ts[1].tv_nsec = 987654300;
	say("utimensat explicit = %d", lk_syscall4(NR_utimensat, AT_FDCWD, "target.txt", ts, 0));
	xstat("target.txt", &st);
	say("atime = %d.%d mtime = %d.%d", st.st_atim.tv_sec, st.st_atim.tv_nsec, st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
	ts[0].tv_nsec = UTIME_OMIT;
	ts[1].tv_sec = 1500000000; ts[1].tv_nsec = 500;	/* NTFS keeps 100 ns */
	say("utimensat OMIT atime = %d", lk_syscall4(NR_utimensat, AT_FDCWD, "target.txt", ts, 0));
	xstat("target.txt", &st);
	say("atime = %d.%d mtime = %d.%d", st.st_atim.tv_sec, st.st_atim.tv_nsec, st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
	ts[1].tv_nsec = UTIME_NOW;
	say("utimensat NOW mtime = %d", lk_syscall4(NR_utimensat, AT_FDCWD, "target.txt", ts, 0));
	xstat("target.txt", &st);
	say("mtime advanced = %d atime kept = %d", (long)(st.st_mtim.tv_sec > 1600000000), (long)(st.st_atim.tv_sec == 1600000000));
	ts[0].tv_nsec = 2000000000;
	say("utimensat bad nsec = %d", lk_syscall4(NR_utimensat, AT_FDCWD, "target.txt", ts, 0));
	ts[0].tv_sec = 1400000000; ts[0].tv_nsec = 0;
	ts[1].tv_sec = 1400000001; ts[1].tv_nsec = 0;
	say("utimensat on link NOFOLLOW = %d", lk_syscall4(NR_utimensat, AT_FDCWD, "link", ts, AT_SYMLINK_NOFOLLOW));
	xlstat("link", &lst);
	xstat("target.txt", &st);
	say("link mtime = %d target untouched = %d", lst.st_mtim.tv_sec, (long)(st.st_mtim.tv_sec != 1400000001));
	fd = xopen("target.txt", O_RDWR, 0);
	ts[0].tv_sec = 1300000000; ts[1].tv_sec = 1300000001;
	say("futimens via utimensat(fd, NULL) = %d", lk_syscall4(NR_utimensat, fd, 0, ts, 0));
	xfstat(fd, &st);
	say("mtime = %d", st.st_mtim.tv_sec);
	say("ftruncate 100 = %d", lk_syscall2(NR_ftruncate, fd, 100));
	xfstat(fd, &st);
	say("size = %d", st.st_size);
	say("ftruncate -1 = %d", lk_syscall2(NR_ftruncate, fd, -1));
	xclose(fd);
	say("truncate 3 = %d", lk_syscall2(NR_truncate, "target.txt", 3));
	xstat("target.txt", &st);
	say("size = %d", st.st_size);
	say("truncate dir = %d", lk_syscall2(NR_truncate, "d", 0));
	say("truncate absent = %d", lk_syscall2(NR_truncate, "absent", 0));
	fd = xopen("target.txt", O_RDONLY, 0);
	say("ftruncate O_RDONLY = %d", lk_syscall2(NR_ftruncate, fd, 1));
	xclose(fd);
	r = lk_syscall2(NR_access, "target.txt", R_OK);
	say("access R_OK = %d", r);
	say("access X_OK on 0640 = %d", lk_syscall2(NR_access, "target.txt", X_OK));
	say("chmod 0750 = %d", lk_syscall2(NR_chmod, "target.txt", 0750));
	say("access X_OK on 0750 = %d", lk_syscall2(NR_access, "target.txt", X_OK));
	say("access F_OK absent = %d", lk_syscall2(NR_access, "absent", F_OK));
	say("access d X_OK = %d", lk_syscall2(NR_access, "d", X_OK));
	say("access bad mode = %d", lk_syscall2(NR_access, "d", 8));
}

static int cmp_names(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char)*a - (unsigned char)*b;
}

static void list_dir(const char *what, long fd)
{
	char buf[4096];
	char names[64][64];
	int types[64], n = 0, i, j;
	long got;
	for (;;) {
		long off = 0;
		got = lk_syscall3(NR_getdents64, fd, buf, sizeof buf);
		if (got <= 0) break;
		while (off < got && n < 64) {
			struct dirent64 *d = (struct dirent64 *)(buf + off);
			scpy(names[n], d->d_name);
			types[n] = d->d_type;
			n++;
			off += d->d_reclen;
		}
	}
	/* insertion sort, so the listing does not depend on the host's order */
	for (i = 1; i < n; i++) {
		char t[64]; int tt = types[i];
		scpy(t, names[i]);
		for (j = i; j > 0 && cmp_names(names[j - 1], t) > 0; j--) { scpy(names[j], names[j - 1]); types[j] = types[j - 1]; }
		scpy(names[j], t); types[j] = tt;
	}
	puts_(what); puts_(" = "); putnum(got, 10); puts_(" entries:");
	for (i = 0; i < n; i++) { putc_(' '); puts_(names[i]); putc_('('); putnum(types[i], 10); putc_(')'); }
	putc_('\n');
}

static void t_dirs(void)
{
	long fd, r;
	struct stat st;
	say("== directories");
	mkfile("d/a", "A"); mkfile("d/b", "B"); mkfile("d/c", "C");
	lk_syscall2(NR_mkdir, "d/e", 0755);
	lk_syscall2(NR_symlink, "a", "d/l");
	fd = xopen("d", O_RDONLY | O_DIRECTORY, 0);
	say("open d O_DIRECTORY ok = %d", (long)(fd >= 0));
	list_dir("getdents64 d", fd);
	say("getdents64 again = %d", lk_syscall3(NR_getdents64, fd, (char[512]){0}, 512));
	say("lseek dir 0 = %d", lk_syscall3(NR_lseek, fd, 0, SEEK_SET));
	list_dir("getdents64 after rewind", fd);
	say("read on dir = %d", xread(fd, (char[16]){0}, 16));
	r = xopen("target.txt", O_RDONLY, 0);
	say("getdents64 on file = %d", lk_syscall3(NR_getdents64, r, (char[512]){0}, 512));
	xclose(r);
	say("getdents64 bad fd = %d", lk_syscall3(NR_getdents64, 999, (char[512]){0}, 512));
	say("fchdir d = %d", lk_syscall1(NR_fchdir, fd));
	say("stat a from d = %d", xstat("a", &st));
	say("stat ../target.txt = %d", xstat("../target.txt", &st));
	say("chdir .. = %d", lk_syscall1(NR_chdir, ".."));
	say("stat d/a = %d", xstat("d/a", &st));
	say("chdir absent = %d", lk_syscall1(NR_chdir, "absent"));
	say("chdir target.txt = %d", lk_syscall1(NR_chdir, "target.txt"));
	say("chdir link-to-dir dd = %d", lk_syscall1(NR_chdir, "dd"));
	say("stat a = %d", xstat("a", &st));
	say("chdir .. = %d", lk_syscall1(NR_chdir, ".."));
	{
		char cwd[512], cwd2[512];
		long n = lk_syscall2(NR_getcwd, cwd, sizeof cwd);
		say("getcwd = %d", (long)(n > 1));
		say("getcwd small = %d", lk_syscall2(NR_getcwd, cwd2, 2));
		lk_syscall1(NR_chdir, "d");
		lk_syscall2(NR_getcwd, cwd2, sizeof cwd2);
		say("cwd/d ends with /d = %d", (long)(seq(cwd2 + slen(cwd2) - 2, "/d")));
		lk_syscall1(NR_chdir, "..");
		lk_syscall2(NR_getcwd, cwd2, sizeof cwd2);
		say("back = %d", (long)seq(cwd, cwd2));
	}
	/* the *at family through the directory descriptor */
	r = lk_syscall4(NR_openat, fd, "a", O_RDONLY, 0);
	say("openat dfd a = %d", r >= 0 ? 0 : r);
	xclose(r);
	r = lk_syscall4(NR_openat, fd, "../target.txt", O_RDONLY, 0);
	say("openat dfd ../target.txt = %d", r >= 0 ? 0 : r);
	xclose(r);
	say("openat bad dfd = %d", lk_syscall4(NR_openat, 999, "a", O_RDONLY, 0));
	r = xopen("target.txt", O_RDONLY, 0);
	say("openat file as dfd = %d", lk_syscall4(NR_openat, r, "a", O_RDONLY, 0));
	xclose(r);
	say("mkdirat dfd m = %d", lk_syscall3(NR_mkdirat, fd, "m", 0755));
	show_stat("stat", "d/m", 1);
	say("unlinkat dfd m REMOVEDIR = %d", lk_syscall3(NR_unlinkat, fd, "m", AT_REMOVEDIR));
	say("unlinkat dfd a REMOVEDIR = %d", lk_syscall3(NR_unlinkat, fd, "a", AT_REMOVEDIR));
	say("unlinkat dfd e (dir, no flag) = %d", lk_syscall3(NR_unlinkat, fd, "e", 0));
	say("renameat dfd a a2 = %d", lk_syscall4(NR_renameat, fd, "a", fd, "a2"));
	say("fstatat dfd a2 = %d", lk_syscall4(NR_newfstatat, fd, "a2", &st, 0));
	r = lk_syscall4(NR_newfstatat, fd, "", &st, AT_EMPTY_PATH);
	say("fstatat dfd empty EMPTY_PATH = %d mode %o", r, (long)st.st_mode);
	say("fstatat dfd empty no flag = %d", lk_syscall4(NR_newfstatat, fd, "", &st, 0));
	r = lk_syscall4(NR_newfstatat, fd, "l", &st, AT_SYMLINK_NOFOLLOW);
	say("fstatat dfd l NOFOLLOW = %d mode %o", r, (long)st.st_mode);
	say("fstatat dfd l follow = %d", lk_syscall4(NR_newfstatat, fd, "l", &st, 0));
	say("linkat AT_FDCWD target.txt dfd t2 = %d", lk_syscall5(NR_linkat, AT_FDCWD, "target.txt", fd, "t2", 0));
	show_stat("stat", "d/t2", 1);
	say("symlinkat = %d", lk_syscall3(NR_symlinkat, "t2", fd, "ls2"));
	{
		char b[64];
		r = lk_syscall4(NR_readlinkat, fd, "ls2", b, 63);
		if (r >= 0) b[r] = 0;
		say("readlinkat = %d %s", r, b);
	}
	say("fchmodat dfd t2 0600 = %d", lk_syscall4(NR_fchmodat, fd, "t2", 0600, 0));
	show_stat("stat", "target.txt", 1);
	say("fchownat dfd ls2 NOFOLLOW 9 9 = %d", lk_syscall5(NR_fchownat, fd, "ls2", 9, 9, AT_SYMLINK_NOFOLLOW));
	show_stat("lstat", "d/ls2", 0);
	say("faccessat dfd t2 W_OK = %d", lk_syscall4(NR_faccessat, fd, "t2", W_OK, 0));
	say("unlinkat dfd t2 = %d", lk_syscall3(NR_unlinkat, fd, "t2", 0));
	say("unlinkat dfd ls2 = %d", lk_syscall3(NR_unlinkat, fd, "ls2", 0));
	xclose(fd);
	say("mknod fifo = %d", lk_syscall4(NR_mknodat, AT_FDCWD, "fifo", S_IFIFO | 0644, 0));
	show_stat("stat", "fifo", 1);
	say("unlink fifo = %d", lk_syscall1(NR_unlink, "fifo"));
	say("mknod regular = %d", lk_syscall4(NR_mknodat, AT_FDCWD, "reg", 0100600, 0));
	show_stat("stat", "reg", 1);
	say("unlink reg = %d", lk_syscall1(NR_unlink, "reg"));
}

static void t_descriptors(void)
{
	long fd, d2, r;
	char b[32];
	int p[2];
	struct stat st;
	say("== descriptors and pipes");
	fd = xopen("target.txt", O_RDWR, 0);
	d2 = lk_syscall1(NR_dup, fd);
	say("dup > fd = %d", (long)(d2 > fd));
	say("write via fd = %d", xwrite(fd, "12345"));
	say("lseek via dup CUR = %d", lk_syscall3(NR_lseek, d2, 0, SEEK_CUR));
	say("dup2 fd 20 = %d", lk_syscall2(NR_dup2, fd, 20));
	say("dup2 same = %d", lk_syscall2(NR_dup2, fd, fd));
	say("dup2 bad = %d", lk_syscall2(NR_dup2, 999, 21));
	say("dup3 same = %d", lk_syscall3(NR_dup3, fd, fd, 0));
	say("dup3 cloexec 22 = %d", lk_syscall3(NR_dup3, fd, 22, O_CLOEXEC));
	say("fcntl 22 GETFD = %d", lk_syscall3(NR_fcntl, 22, F_GETFD, 0));
	say("fcntl 20 GETFD = %d", lk_syscall3(NR_fcntl, 20, F_GETFD, 0));
	say("fcntl 20 SETFD cloexec = %d", lk_syscall3(NR_fcntl, 20, F_SETFD, FD_CLOEXEC));
	say("fcntl 20 GETFD = %d", lk_syscall3(NR_fcntl, 20, F_GETFD, 0));
	say("fcntl GETFL accmode = %d", lk_syscall3(NR_fcntl, fd, F_GETFL, 0) & 3);
	say("fcntl SETFL append = %d", lk_syscall3(NR_fcntl, fd, F_SETFL, O_APPEND));
	say("fcntl GETFL has append = %d", (long)((lk_syscall3(NR_fcntl, fd, F_GETFL, 0) & O_APPEND) != 0));
	say("fcntl DUPFD 30 = %d", lk_syscall3(NR_fcntl, fd, F_DUPFD, 30));
	say("fcntl bad cmd = %d", lk_syscall3(NR_fcntl, fd, 9999, 0));
	xclose(fd); xclose(d2); xclose(20); xclose(22); xclose(30);
	say("fcntl closed = %d", lk_syscall3(NR_fcntl, fd, F_GETFD, 0));
	say("pipe = %d", lk_syscall1(NR_pipe, p));
	say("pipe fds ascending = %d", (long)(p[1] == p[0] + 1));
	say("write pipe = %d", xwrite(p[1], "pipe!"));
	say("read pipe = %d %s", (r = xread(p[0], b, 31)), b);
	say("lseek pipe = %d", lk_syscall3(NR_lseek, p[0], 0, SEEK_CUR));
	say("write to read end = %d", xwrite(p[0], "x"));
	say("read from write end = %d", xread(p[1], b, 4));
	r = xfstat(p[0], &st);
	say("fstat pipe = %d mode %o", r, (long)st.st_mode);
	say("close write end = %d", xclose(p[1]));
	say("read at eof = %d", xread(p[0], b, 31));
	xclose(p[0]);
	say("pipe2 NONBLOCK = %d", lk_syscall2(NR_pipe2, p, O_NONBLOCK));
	say("read empty nonblock = %d", xread(p[0], b, 4));
	say("fcntl GETFL pipe nonblock = %d", (long)((lk_syscall3(NR_fcntl, p[0], F_GETFL, 0) & O_NONBLOCK) != 0));
	xclose(p[0]); xclose(p[1]);
	say("pipe2 bad flag = %d", lk_syscall2(NR_pipe2, p, 0x80000000));
	say("read bad fd = %d", xread(999, b, 4));
	say("close -1 = %d", xclose(-1));
	{
		struct { const char *base; unsigned long len; } iov[3] = { { "ab", 2 }, { "", 0 }, { "cde", 3 } };
		fd = xopen("wv", O_CREAT | O_WRONLY | O_TRUNC, 0644);
		say("writev = %d", lk_syscall3(NR_writev, fd, iov, 3));
		xclose(fd);
		xstat("wv", &st);
		say("wv size = %d", st.st_size);
		lk_syscall1(NR_unlink, "wv");
	}
}

static void t_special(void)
{
	long fd, r;
	char b[64];
	struct stat st;
	struct statx sx;
	say("== /dev, /proc and statx");
	fd = xopen("/dev/null", O_RDWR, 0);
	say("/dev/null write = %d read = %d", xwrite(fd, "abc"), xread(fd, b, 10));
	xfstat(fd, &st);
	say("/dev/null fstat mode = %o rdev major %d minor %d", (long)st.st_mode, (long)((st.st_rdev >> 8) & 0xfff), (long)(st.st_rdev & 0xff));
	xclose(fd);
	fd = xopen("/dev/zero", O_RDONLY, 0);
	r = xread(fd, b, 4);
	say("/dev/zero read = %d zeros = %d", r, (long)(b[0] == 0 && b[3] == 0));
	xclose(fd);
	fd = xopen("/dev/full", O_WRONLY, 0);
	say("/dev/full write = %d", xwrite(fd, "x"));
	xclose(fd);
	fd = xopen("/dev/urandom", O_RDONLY, 0);
	say("/dev/urandom read = %d", xread(fd, b, 8));
	xclose(fd);
	xstat("/dev/null", &st);
	say("stat /dev/null mode = %o", (long)st.st_mode);
	xlstat("/dev/stdin", &st);
	say("lstat /dev/stdin is link = %d", (long)((st.st_mode & S_IFMT) == 0120000));
	say("open /dev/absent = %d", xopen("/dev/absent", O_RDONLY, 0));
	fd = xopen("/dev", O_RDONLY | O_DIRECTORY, 0);
	say("open /dev O_DIRECTORY ok = %d", (long)(fd >= 0));
	xclose(fd);
	r = lk_syscall3(NR_readlink, "/proc/self/exe", b, 63);
	say("readlink /proc/self/exe nonempty = %d", (long)(r > 0));
	r = lk_syscall3(NR_readlink, "/proc/self/cwd", b, 63);
	say("readlink /proc/self/cwd nonempty = %d", (long)(r > 0));
	fd = xopen("target.txt", O_RDONLY, 0);
	{
		char p[64];
		p[0] = '/'; p[1] = 'p'; p[2] = 'r'; p[3] = 'o'; p[4] = 'c'; p[5] = '/'; p[6] = 's'; p[7] = 'e'; p[8] = 'l'; p[9] = 'f'; p[10] = '/'; p[11] = 'f'; p[12] = 'd'; p[13] = '/';
		p[14] = (char)('0' + fd); p[15] = 0;
		r = lk_syscall3(NR_readlink, p, b, 63);
		if (r > 0) b[r] = 0;
		say("readlink /proc/self/fd/N ends with target.txt = %d", (long)(r > 10 && seq(b + r - 10, "target.txt")));
		xstat(p, &st);
		say("stat /proc/self/fd/N mode = %o", (long)st.st_mode);
	}
	xclose(fd);
	xlstat("/proc/self", &st);
	say("lstat /proc/self is link = %d", (long)((st.st_mode & S_IFMT) == 0120000));
	xstat("/proc/self", &st);
	say("stat /proc/self mode = %o", (long)st.st_mode);
	fd = xopen("/proc/self/maps", O_RDONLY, 0);
	r = xread(fd, b, 63);
	say("/proc/self/maps read nonempty = %d", (long)(r > 0));
	xclose(fd);
	say("open /proc/self/maps O_WRONLY = %d", xopen("/proc/self/maps", O_WRONLY, 0));
	say("open /proc/absent = %d", xopen("/proc/absent", O_RDONLY, 0));
	r = lk_syscall5(NR_statx, AT_FDCWD, "target.txt", 0, STATX_BASIC_STATS | STATX_BTIME, &sx);
	say("statx = %d mode %o size %d uid %d btime present = %d", r, (long)sx.stx_mode, (long)sx.stx_size, (long)sx.stx_uid, (long)((sx.stx_mask & STATX_BTIME) != 0));
	r = lk_syscall5(NR_statx, AT_FDCWD, "link", AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS, &sx);
	say("statx link NOFOLLOW = %d mode %o", r, (long)sx.stx_mode);
	r = lk_syscall5(NR_statx, AT_FDCWD, "absent", 0, STATX_BASIC_STATS, &sx);
	say("statx absent = %d", r);
}

static void t_cleanup(void)
{
	lk_syscall1(NR_unlink, "d/a2"); lk_syscall1(NR_unlink, "d/b"); lk_syscall1(NR_unlink, "d/c");
	lk_syscall1(NR_unlink, "d/l"); lk_syscall1(NR_unlink, "d/dl"); lk_syscall1(NR_rmdir, "d/e");
	say("rmdir d = %d", lk_syscall1(NR_rmdir, "d"));
	lk_syscall1(NR_unlink, "dd"); lk_syscall1(NR_unlink, "link"); lk_syscall1(NR_unlink, "target.txt");
	lk_syscall1(NR_unlink, "masked2"); lk_syscall1(NR_unlink, "a:b*c?d");
	lk_syscall1(NR_chdir, "..");
	say("rmdir w = %d", lk_syscall1(NR_rmdir, "w"));
	say("== end");
}

int lk_main(int argc, char **argv, char **envp)
{
	(void)argc; (void)argv; (void)envp;
	/* work in a subdirectory, so the working directory is the same shape
	 * (a name under something) on both hosts */
	if (lk_syscall2(NR_mkdir, "w", 0755) != 0 || lk_syscall1(NR_chdir, "w") != 0) {
		say("cannot make the working directory");
		flush();
		return 1;
	}
	t_create();
	t_rename_unlink();
	t_links();
	t_attrs();
	t_dirs();
	t_descriptors();
	t_special();
	t_cleanup();
	flush();
	return 0;
}
