/*
 * lxfs-tree.c -- criterion 3's program: write a tree carrying every kind of
 * metadata the VFS keeps on NTFS, or read one back as a manifest.
 *
 * Freestanding against lksys.h like vfs-trace.c, built for the gate and for
 * the oracle.  `write` builds the tree under d1 in the working directory;
 * `read` walks d1 and prints one line per object, sorted by path: the type,
 * mode, owner and group, then the size for a file, the target for a symlink,
 * the device numbers for a device node.  Two manifests of the same tree taken
 * by different readers must be identical, which is what the harness checks.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "lksys.h"

#define NR_write 1
#define NR_open 2
#define NR_close 3
#define NR_lstat 6
#define NR_mkdir 83
#define NR_link 86
#define NR_symlink 88
#define NR_readlink 89
#define NR_chmod 90
#define NR_lchown 94
#define NR_chown 92
#define NR_umask 95
#define NR_getdents64 217
#define NR_mknodat 259

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_DIRECTORY 0200000
#define AT_FDCWD (-100)
#define S_IFMT 0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFLNK 0120000

struct timespec { long tv_sec; long tv_nsec; };
struct stat {
	unsigned long st_dev, st_ino, st_nlink;
	unsigned st_mode, st_uid, st_gid, pad0;
	unsigned long st_rdev;
	long st_size, st_blksize, st_blocks;
	struct timespec st_atim, st_mtim, st_ctim;
	long unused[3];
};
struct dirent64 { unsigned long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };

static char obuf[16384];
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
static void putc_(char c) { if (olen >= (int)sizeof obuf) flush(); obuf[olen++] = c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putnum(long v, int base)
{
	char tmp[32]; int i = 0; unsigned long u;
	if (v < 0) { putc_('-'); u = (unsigned long)(-v); } else u = (unsigned long)v;
	do { int d = (int)(u % (unsigned long)base); tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned long)base; } while (u);
	while (i) putc_(tmp[--i]);
}
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)); }
static void scat(char *d, const char *s) { while (*d) d++; scpy(d, s); }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

/* ---- write ---------------------------------------------------------------- */

static long mkfile(const char *p, const char *content, int mode)
{
	long fd = lk_syscall3(NR_open, p, O_CREAT | O_WRONLY | O_TRUNC, mode);
	if (fd < 0) return fd;
	if (content) lk_syscall3(NR_write, fd, content, slen(content));
	lk_syscall1(NR_close, fd);
	return 0;
}

static int fails;
static void must(long r, const char *what)
{
	if (r < 0) { puts_("write failed: "); puts_(what); puts_(" = "); putnum(r, 10); putc_('\n'); fails++; }
}

static void do_write(void)
{
	lk_syscall1(NR_umask, 022);
	must(lk_syscall2(NR_mkdir, "d1", 0750), "mkdir d1");
	must(lk_syscall3(NR_chown, "d1", 1000, 1000), "chown d1");
	must(mkfile("d1/f644", "six44\n", 0644), "f644");
	must(mkfile("d1/f755", "seven55\n", 0755), "f755");
	must(mkfile("d1/f600", "", 0600), "f600");
	must(lk_syscall3(NR_chown, "d1/f600", 1234, 5678), "chown f600");
	must(mkfile("d1/sticky", "", 01777 & 0777), "sticky");
	must(lk_syscall2(NR_chmod, "d1/sticky", 04751), "chmod setuid");
	must(lk_syscall2(NR_mkdir, "d1/sub", 0700), "mkdir sub");
	must(mkfile("d1/sub/deep", "deep\n", 0644), "deep");
	must(lk_syscall2(NR_symlink, "f644", "d1/rel"), "symlink rel");
	must(lk_syscall2(NR_symlink, "/etc/hostname", "d1/abs"), "symlink abs");
	must(lk_syscall2(NR_symlink, "sub", "d1/dir"), "symlink dir");
	must(lk_syscall2(NR_symlink, "sub/deep", "d1/down"), "symlink down");
	must(lk_syscall2(NR_symlink, "../d1/f755", "d1/sub/up"), "symlink up");
	must(lk_syscall2(NR_symlink, "nowhere", "d1/dangling"), "symlink dangling");
	must(lk_syscall3(NR_lchown, "d1/rel", 42, 43), "lchown rel");
	must(lk_syscall4(NR_mknodat, AT_FDCWD, "d1/fifo", S_IFIFO | 0644, 0), "mknod fifo");
	must(lk_syscall4(NR_mknodat, AT_FDCWD, "d1/null", S_IFCHR | 0666, (1 << 8) | 3), "mknod null");
	must(lk_syscall2(NR_link, "d1/f644", "d1/hard"), "link hard");
	must(mkfile("d1/odd:name*?", "odd\n", 0644), "odd name");
	must(mkfile("d1/caf\xc3\xa9", "utf8\n", 0644), "utf-8 name");
	must(mkfile("d1/Makefile", "M\n", 0644), "Makefile");
	must(mkfile("d1/makefile", "m\n", 0644), "makefile");
	must(mkfile("d1/.hidden", "", 0600), ".hidden");
	must(mkfile("d1/a b", "space\n", 0644), "space name");
}

/* ---- read ------------------------------------------------------------------ */

static char lines[256][320];
static int nlines;

static void note_line(const char *s)
{
	if (nlines < 256) scpy(lines[nlines++], s);
}

static void describe(const char *path)
{
	struct stat st;
	char line[320], b[64];
	long r = lk_syscall2(NR_lstat, path, &st);
	scpy(line, path);
	scat(line, " ");
	if (r) { scat(line, "lstat="); { char n[16]; int i = 0; long v = -r; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0; while (i > 0) { char t = n[--i]; char s2[2] = { t, 0 }; scat(line, s2); } } note_line(line); return; }
	{
		unsigned m = st.st_mode;
		const char *t = (m & S_IFMT) == S_IFDIR ? "dir" : (m & S_IFMT) == S_IFLNK ? "link"
			: (m & S_IFMT) == S_IFIFO ? "fifo" : (m & S_IFMT) == S_IFCHR ? "chr" : "file";
		char oct[16]; int i = 0; unsigned v = m & 07777;
		scat(line, t); scat(line, " ");
		do { oct[i++] = (char)('0' + v % 8); v /= 8; } while (v);
		while (i < 4) oct[i++] = '0';
		oct[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = oct[j]; oct[j] = oct[i - 1 - j]; oct[i - 1 - j] = tmp; }
		scat(line, oct);
	}
	{
		char n[24]; int i; unsigned long v;
		scat(line, " "); v = st.st_uid; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n);
		scat(line, ":"); v = st.st_gid; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n);
	}
	if ((st.st_mode & S_IFMT) == S_IFLNK) {
		long n = lk_syscall3(NR_readlink, path, b, sizeof b - 1);
		if (n < 0) n = 0;
		b[n] = 0;
		scat(line, " -> "); scat(line, b);
	} else if ((st.st_mode & S_IFMT) == S_IFCHR) {
		char n[24]; int i; unsigned long v;
		scat(line, " dev "); v = (st.st_rdev >> 8) & 0xfff; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n); scat(line, ":");
		v = st.st_rdev & 0xff; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n);
	} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
		char n[24]; int i; unsigned long v;
		scat(line, " size "); v = (unsigned long)st.st_size; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n);
		scat(line, " nlink "); v = st.st_nlink; i = 0; do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v); n[i] = 0;
		for (int j = 0; j < i / 2; j++) { char tmp = n[j]; n[j] = n[i - 1 - j]; n[i - 1 - j] = tmp; }
		scat(line, n);
	}
	note_line(line);
}

static void walk(const char *dir)
{
	char buf[8192];
	char names[128][80];
	unsigned char types[128];
	int n = 0, i;
	long fd = lk_syscall3(NR_open, dir, O_RDONLY | O_DIRECTORY, 0);
	if (fd < 0) return;
	for (;;) {
		long got = lk_syscall3(NR_getdents64, fd, buf, sizeof buf), off = 0;
		if (got <= 0) break;
		while (off < got && n < 128) {
			struct dirent64 *d = (struct dirent64 *)(buf + off);
			if (!(d->d_name[0] == '.' && (d->d_name[1] == 0 || (d->d_name[1] == '.' && d->d_name[2] == 0)))) {
				scpy(names[n], d->d_name);
				types[n] = d->d_type;
				n++;
			}
			off += d->d_reclen;
		}
	}
	lk_syscall1(NR_close, fd);
	for (i = 0; i < n; i++) {
		char path[320];
		scpy(path, dir); scat(path, "/"); scat(path, names[i]);
		describe(path);
		if (types[i] == 4) walk(path);
	}
}

static void do_read(void)
{
	int i, j;
	describe("d1");
	walk("d1");
	for (i = 1; i < nlines; i++) {
		char t[320];
		scpy(t, lines[i]);
		for (j = i; j > 0 && scmp(lines[j - 1], t) > 0; j--) scpy(lines[j], lines[j - 1]);
		scpy(lines[j], t);
	}
	for (i = 0; i < nlines; i++) { puts_(lines[i]); putc_('\n'); }
}

int lk_main(int argc, char **argv, char **envp)
{
	(void)envp;
	if (argc >= 2 && argv[1][0] == 'w') { do_write(); flush(); return fails ? 1 : 0; }
	if (argc >= 2 && argv[1][0] == 'r') { do_read(); flush(); return 0; }
	puts_("usage: lxfs-tree write|read\n");
	flush();
	return 2;
}
