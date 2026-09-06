/*
 * hostfs-test.c -- the certification bar for core/hostfs_nt.c, the host file
 * store beneath the VFS.
 *
 * Each group opens a fresh scratch directory (given on the command line, on a
 * local NTFS volume) and exercises one contract of hostfs.h: creation carries
 * the LX attributes, stat reads them back, bytes go where the offset says,
 * names are escaped and unescaped, unlink and rename keep POSIX semantics,
 * links and symlinks and special files round-trip through their reparse tags,
 * directories enumerate without "." and "..".  The expected behaviours are
 * the ones spike 39 measured on this volume; a group that fails on another
 * volume is a fact about that volume, which hfs_volume_is_lx_capable reports
 * first so the transcript says which.
 *
 * Usage: hostfs-test [-q] SCRATCH-DIR   (exit 0 iff every group passes)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hostfs.h"
#include "lxerrno.h"

static int quiet, failures, checks;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("FAIL %s\n", what);
	} else if (!quiet) {
		printf("ok   %s\n", what);
	}
}

static hfs_h root;

static hfs_h mk(const char *name, uint32_t mode, const char *content)
{
	hfs_h h = 0;
	int r = hfs_openat(root, name, HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE | HFS_O_TRUNC, mode, 1000, 1000, &h, NULL);
	if (r) return 0;
	if (content) hfs_pwrite(h, content, strlen(content), 0, 0);
	return h;
}

static void group_volume(void)
{
	int cap = hfs_volume_is_lx_capable(root);
	ok(cap == 1, "volume is NTFS with EAs and POSIX delete/rename");
	ok(hfs_volume_serial(root) != 0, "volume serial reads");
	ok(hfs_now() > 1600000000LL * 1000000000LL, "host clock is past 2020");
}

static void group_create_stat(void)
{
	struct hfs_stat st;
	hfs_h h = 0;
	int r = hfs_openat(root, "plain.txt", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE | HFS_O_EXCL, 0640, 1000, 1001, &h, &st);
	ok(r == 0 && h, "create O_EXCL a new file");
	ok(st.kind == HFS_KIND_FILE, "created object is a file");
	ok((st.lxflags & (HFS_LX_UID | HFS_LX_GID | HFS_LX_MODE)) == (HFS_LX_UID | HFS_LX_GID | HFS_LX_MODE), "creation wrote the three LX attributes");
	ok(st.mode == 0640 && st.uid == 1000 && st.gid == 1001, "mode, uid and gid read back as created");
	ok(st.nlink == 1 && st.size == 0 && st.ino != 0, "nlink 1, size 0, an inode number");
	ok(st.btime > 0 && st.mtime > 0 && st.ctime > 0, "times are set");
	hfs_close(h);
	r = hfs_openat(root, "plain.txt", HFS_O_READ | HFS_O_CREATE | HFS_O_EXCL, 0600, 0, 0, &h, NULL);
	ok(r == -EEXIST, "O_EXCL on an existing name is EEXIST");
	r = hfs_openat(root, "absent.txt", HFS_O_READ, 0, 0, 0, &h, NULL);
	ok(r == -ENOENT, "open of an absent name is ENOENT");
	r = hfs_statat(root, "plain.txt", &st);
	ok(r == 0 && st.mode == 0640, "statat reads the LX mode without keeping a handle");
	r = hfs_openat(root, "plain.txt", HFS_O_READ | HFS_O_DIR, 0, 0, 0, &h, NULL);
	ok(r == -ENOTDIR, "O_DIRECTORY on a file is ENOTDIR");
	r = hfs_mkdir(root, "d", 0750, 1000, 1000, 0);
	ok(r == 0, "mkdir with a mode");
	r = hfs_openat(root, "d", HFS_O_READ | HFS_O_NODIR, 0, 0, 0, &h, NULL);
	ok(r == -EISDIR, "a non-directory open of a directory is EISDIR");
	r = hfs_openat(root, "d", HFS_O_READ | HFS_O_DIR, 0, 0, 0, &h, &st);
	ok(r == 0 && st.kind == HFS_KIND_DIR && st.mode == 0750, "the directory opens and carries its mode");
	hfs_close(h);
	r = hfs_mkdir(root, "d", 0750, 1000, 1000, 0);
	ok(r == -EEXIST, "mkdir over an existing name is EEXIST");
}

static void group_bytes(void)
{
	char buf[64];
	struct hfs_stat st;
	hfs_h h = mk("bytes.txt", 0644, "hello, world");
	int64_t n;
	ok(h != 0, "file with content");
	n = hfs_pread(h, buf, sizeof buf, 0);
	ok(n == 12 && memcmp(buf, "hello, world", 12) == 0, "pread at 0 returns the content");
	n = hfs_pread(h, buf, 5, 7);
	ok(n == 5 && memcmp(buf, "world", 5) == 0, "pread at an offset");
	n = hfs_pread(h, buf, sizeof buf, 12);
	ok(n == 0, "pread at end of file is 0");
	n = hfs_pread(h, buf, sizeof buf, 100);
	ok(n == 0, "pread past end of file is 0");
	n = hfs_pwrite(h, "HELLO", 5, 0, 0);
	ok(n == 5, "pwrite at 0");
	n = hfs_pwrite(h, "!", 1, 0, 1);
	ok(n == 1, "append write");
	n = hfs_pread(h, buf, sizeof buf, 0);
	ok(n == 13 && memcmp(buf, "HELLO, world!", 13) == 0, "the file reads HELLO, world!");
	n = hfs_pwrite(h, "x", 1, 20, 0);
	ok(n == 1, "write past end extends the file");
	hfs_stat(h, &st);
	ok(st.size == 21, "size is 21 after the sparse write");
	n = hfs_pread(h, buf, 8, 13);
	ok(n == 8 && buf[0] == 0 && buf[6] == 0 && buf[7] == 'x', "the hole reads as zeros");
	ok(hfs_truncate(h, 5) == 0, "truncate to 5");
	hfs_stat(h, &st);
	ok(st.size == 5, "size is 5 after truncate");
	ok(hfs_fsync(h) == 0, "fsync");
	hfs_close(h);
}

static void group_attrs(void)
{
	struct hfs_stat st;
	hfs_h h = mk("attrs.txt", 0644, NULL);
	int64_t t = 1700000000LL * 1000000000LL + 123456700;	/* 100 ns resolution */
	ok(h != 0, "file for attributes");
	ok(hfs_set_lx(h, HFS_LX_MODE, 0, 0, 0755, 0, 0) == 0, "chmod through the EA");
	hfs_stat(h, &st);
	ok(st.mode == 0755 && st.uid == 1000, "mode changed, uid kept");
	ok(hfs_set_lx(h, HFS_LX_UID | HFS_LX_GID, 42, 43, 0, 0, 0) == 0, "chown through the EAs");
	hfs_stat(h, &st);
	ok(st.uid == 42 && st.gid == 43 && st.mode == 0755, "uid and gid changed, mode kept");
	ok(hfs_set_times(h, t, t + 1000) == 0, "utimens with explicit times");
	hfs_stat(h, &st);
	ok(st.atime == t && st.mtime == t + 1000, "atime and mtime read back at 100 ns");
	ok(hfs_set_times(h, HFS_TIME_OMIT, HFS_TIME_NOW) == 0, "utimens with OMIT and NOW");
	hfs_stat(h, &st);
	ok(st.atime == t && st.mtime > t + 1000, "atime kept, mtime advanced");
	hfs_close(h);
}

static void group_names(void)
{
	struct hfs_dirent ents[16];
	struct hfs_stat st;
	struct hfs_dir *dr;
	hfs_h h, d;
	int n, i, seen = 0;
	const char *odd = "a:b*c?d\"e<f>g|h\\i";
	h = mk(odd, 0644, "odd");
	ok(h != 0, "a name with every character NTFS refuses");
	hfs_close(h);
	ok(hfs_statat(root, odd, &st) == 0 && st.size == 3, "the escaped name opens again");
	h = mk("caf\xc3\xa9.txt", 0644, NULL);
	ok(h != 0, "a UTF-8 name");
	hfs_close(h);
	ok(hfs_openat(root, "names", HFS_O_READ | HFS_O_DIR, 0, 0, 0, &d, NULL) == -ENOENT, "the names directory is not there yet");
	ok(hfs_mkdir(root, "names", 0755, 0, 0, 0) == 0, "mkdir names");
	ok(hfs_openat(root, "names", HFS_O_READ | HFS_O_DIR, 0, 0, 0, &d, NULL) == 0, "open names/");
	hfs_close(mk("names/x", 0644, NULL));	/* a slash in a component is refused below */
	{
		hfs_h f = 0;
		int r = hfs_openat(d, "one", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE, 0644, 0, 0, &f, NULL);
		ok(r == 0, "create one under names/ through the directory handle");
		hfs_close(f);
		r = hfs_openat(d, "two", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE, 0644, 0, 0, &f, NULL);
		hfs_close(f);
		ok(hfs_mkdir(d, "sub", 0755, 0, 0, 0) == 0, "mkdir sub under names/");
	}
	dr = hfs_dir_open(d);
	ok(dr != NULL, "a directory reader");
	n = hfs_dir_read(dr, ents, 16, 1);
	ok(n == 3, "readdir returns three entries, no dot entries");
	for (i = 0; i < n; i++) {
		if (!strcmp(ents[i].name, "one") && ents[i].kind == HFS_KIND_FILE) seen |= 1;
		if (!strcmp(ents[i].name, "two") && ents[i].kind == HFS_KIND_FILE) seen |= 2;
		if (!strcmp(ents[i].name, "sub") && ents[i].kind == HFS_KIND_DIR) seen |= 4;
		if (ents[i].ino == 0) seen |= 8;
	}
	ok(seen == 7, "the three names, kinds and inode numbers");
	n = hfs_dir_read(dr, ents, 16, 0);
	ok(n == 0, "a second read without restart is at the end");
	n = hfs_dir_read(dr, ents, 1, 1);
	ok(n == 1, "a cap of one returns one");
	n = hfs_dir_read(dr, ents, 1, 0);
	ok(n == 1, "and the next one");
	n = hfs_dir_read(dr, ents, 16, 0);
	ok(n == 1, "and the last");
	n = hfs_dir_read(dr, ents, 16, 0);
	ok(n == 0, "and then nothing");
	hfs_dir_close(dr);
	hfs_close(d);
	/* the root's own listing must show the odd name unescaped */
	dr = hfs_dir_open(root);
	n = hfs_dir_read(dr, ents, 16, 1);
	seen = 0;
	for (i = 0; i < n; i++) if (!strcmp(ents[i].name, odd)) seen = 1;
	ok(seen, "the odd name comes back unescaped from the directory");
	hfs_dir_close(dr);
}

static void group_unlink_rename(void)
{
	struct hfs_stat st;
	char buf[16];
	hfs_h h, h2;
	int r;
	h = mk("victim.txt", 0644, "victim");
	ok(h != 0, "file to unlink while open");
	r = hfs_unlink(root, "victim.txt", 0);
	ok(r == 0, "unlink of an open file succeeds (POSIX semantics)");
	ok(hfs_statat(root, "victim.txt", &st) == -ENOENT, "the name is gone at once");
	ok(hfs_pread(h, buf, sizeof buf, 0) == 6, "the open handle still reads");
	h2 = mk("victim.txt", 0644, "again");
	ok(h2 != 0, "the name can be reused while the old file is open");
	hfs_close(h2);
	hfs_close(h);
	ok(hfs_unlink(root, "victim.txt", 0) == 0, "unlink the replacement");
	ok(hfs_unlink(root, "victim.txt", 0) == -ENOENT, "unlink of an absent name is ENOENT");
	ok(hfs_mkdir(root, "rmme", 0755, 0, 0, 0) == 0, "mkdir rmme");
	ok(hfs_unlink(root, "rmme", 0) == -EISDIR, "unlink of a directory is EISDIR");
	hfs_close(mk("keep.txt", 0644, NULL));
	ok(hfs_unlink(root, "keep.txt", 1) == -ENOTDIR, "rmdir of a file is ENOTDIR");
	ok(hfs_unlink(root, "rmme", 1) == 0, "rmdir of an empty directory");
	ok(hfs_mkdir(root, "full", 0755, 0, 0, 0) == 0, "mkdir full");
	hfs_close(mk("full/child", 0644, NULL) ? 0 : 0);
	{
		hfs_h d, f;
		hfs_openat(root, "full", HFS_O_READ | HFS_O_DIR, 0, 0, 0, &d, NULL);
		hfs_openat(d, "child", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE, 0644, 0, 0, &f, NULL);
		hfs_close(f);
		ok(hfs_unlink(root, "full", 1) == -ENOTEMPTY, "rmdir of a non-empty directory is ENOTEMPTY");
		ok(hfs_unlink(d, "child", 0) == 0, "unlink the child through the directory handle");
		hfs_close(d);
		ok(hfs_unlink(root, "full", 1) == 0, "then rmdir succeeds");
	}
	/* rename */
	h = mk("target.txt", 0644, "target");
	hfs_close(mk("source.txt", 0644, "source"));
	r = hfs_rename(root, "source.txt", root, "target.txt", 0);
	ok(r == 0, "rename over an open target succeeds");
	ok(hfs_pread(h, buf, sizeof buf, 0) == 6 && memcmp(buf, "target", 6) == 0, "the old handle still reads the old bytes");
	hfs_close(h);
	ok(hfs_statat(root, "source.txt", &st) == -ENOENT, "the source name is gone");
	ok(hfs_statat(root, "target.txt", &st) == 0 && st.size == 6, "the target name is the source's file");
	hfs_close(mk("noreplace.txt", 0644, NULL));
	r = hfs_rename(root, "target.txt", root, "noreplace.txt", 1);
	ok(r == -EEXIST, "RENAME_NOREPLACE over an existing name is EEXIST");
	r = hfs_rename(root, "absent.txt", root, "whatever", 0);
	ok(r == -ENOENT, "rename of an absent source is ENOENT");
	ok(hfs_mkdir(root, "rd1", 0755, 0, 0, 0) == 0 && hfs_mkdir(root, "rd2", 0755, 0, 0, 0) == 0, "two directories");
	r = hfs_rename(root, "rd1", root, "rd3", 0);
	ok(r == 0, "rename a directory to a new name");
	r = hfs_rename(root, "rd3", root, "rd2", 0);
	/* NTFS with POSIX semantics: a directory over an empty directory; the
	 * transcript records whichever this volume does, the VFS maps it */
	printf("note directory-over-empty-directory rename: %d\n", r);
	ok(r == 0 || r == -EACCES || r == -EEXIST, "directory over directory is 0, EACCES or EEXIST (recorded)");
}

static void group_links(void)
{
	struct hfs_stat st, st2;
	char buf[64];
	hfs_h h, l;
	int r;
	int64_t n;
	h = mk("orig.txt", 0644, "linked");
	ok(hfs_link(h, root, "hard.txt") == 0, "hard link through the open handle");
	hfs_stat(h, &st);
	ok(st.nlink == 2, "nlink is 2");
	ok(hfs_statat(root, "hard.txt", &st2) == 0 && st2.ino == st.ino, "the link has the same inode");
	ok(hfs_link(h, root, "hard.txt") == -EEXIST, "link over an existing name is EEXIST");
	hfs_close(h);
	r = hfs_symlink(root, "sym", "orig.txt", 1000, 1000);
	ok(r == 0, "create an LX symlink without privilege");
	r = hfs_openat(root, "sym", HFS_O_READ, 0, 0, 0, &l, &st);
	ok(r == HFS_REPARSE && st.kind == HFS_KIND_LXLINK, "opening the symlink reports the reparse and the kind");
	ok((st.mode & 0170000) == 0120000, "the symlink's LX mode is S_IFLNK");
	n = hfs_readlink(l, buf, sizeof buf);
	ok(n == 8 && memcmp(buf, "orig.txt", 8) == 0, "readlink returns the target");
	hfs_close(l);
	r = hfs_openat(root, "sym", HFS_O_READ | HFS_O_REPARSE, 0, 0, 0, &l, &st);
	ok(r == 0 && st.kind == HFS_KIND_LXLINK, "O_REPARSE opens the link itself with 0");
	hfs_close(l);
	ok(hfs_statat(root, "sym", &st) == 0 && st.kind == HFS_KIND_LXLINK, "statat does not follow");
	ok(hfs_unlink(root, "sym", 0) == 0, "unlink removes the link, not the target");
	ok(hfs_statat(root, "orig.txt", &st) == 0, "the target survives");
	r = hfs_mknod(root, "fifo", HFS_KIND_FIFO, 010644, 1000, 1000, 0, 0);
	ok(r == 0, "mknod a FIFO");
	ok(hfs_statat(root, "fifo", &st) == 0 && st.kind == HFS_KIND_FIFO, "the FIFO reads back by its tag");
	r = hfs_mknod(root, "chr", HFS_KIND_CHR, 020644, 0, 0, 1, 3);
	ok(r == 0, "mknod a character device");
	ok(hfs_statat(root, "chr", &st) == 0 && st.kind == HFS_KIND_CHR && (st.lxflags & HFS_LX_DEV)
	   && st.rdev_major == 1 && st.rdev_minor == 3, "the device numbers read back");
}

static void group_case(void)
{
	struct hfs_stat st;
	hfs_h d, f;
	int r;
	ok(hfs_mkdir(root, "cs", 0755, 0, 0, 1) == 0, "mkdir cs, case-sensitive at creation");
	ok(hfs_openat(root, "cs", HFS_O_READ | HFS_O_DIR | HFS_O_ATTR, 0, 0, 0, &d, NULL) == 0, "open cs/ for attributes");
	r = hfs_set_case_sensitive(d);
	ok(r == 0, "mark the directory case-sensitive again (idempotent)");
	hfs_stat(d, &st);
	ok(st.lxflags & HFS_LX_CASE, "stat reports the case-sensitive flag");
	ok(hfs_openat(d, "Makefile", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE | HFS_O_EXCL, 0644, 0, 0, &f, NULL) == 0, "create Makefile");
	hfs_close(f);
	ok(hfs_openat(d, "makefile", HFS_O_READ | HFS_O_WRITE | HFS_O_CREATE | HFS_O_EXCL, 0644, 0, 0, &f, NULL) == 0, "create makefile beside it");
	hfs_close(f);
	ok(hfs_statat(d, "MAKEFILE", &st) == -ENOENT, "MAKEFILE is not found in a case-sensitive directory");
	hfs_close(d);
}

static int clean_tree(hfs_h d)
{
	struct hfs_dirent ents[64];
	struct hfs_dir *dr = hfs_dir_open(d);
	int n, i;
	while ((n = hfs_dir_read(dr, ents, 64, 1)) > 0) {
		for (i = 0; i < n; i++) {
			if (ents[i].kind == HFS_KIND_DIR) {
				hfs_h s;
				if (hfs_openat(d, ents[i].name, HFS_O_READ | HFS_O_DIR, 0, 0, 0, &s, NULL) == 0) {
					clean_tree(s);
					hfs_close(s);
				}
				hfs_unlink(d, ents[i].name, 1);
			} else {
				hfs_unlink(d, ents[i].name, 0);
			}
		}
	}
	hfs_dir_close(dr);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = NULL;
	int i, r;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-q")) quiet = 1;
		else if (!strcmp(argv[i], "--version")) { puts("hostfs-test 1.0"); return 0; }
		else dir = argv[i];
	}
	if (!dir) { fprintf(stderr, "usage: hostfs-test [-q] SCRATCH-DIR\n"); return 2; }
	CreateDirectoryA(dir, NULL);
	r = hfs_open_root(dir, &root);
	if (r) { fprintf(stderr, "hostfs-test: cannot open %s: %d\n", dir, r); return 1; }
	clean_tree(root);
	group_volume();
	group_create_stat();
	group_bytes();
	group_attrs();
	group_names();
	group_unlink_rename();
	group_links();
	group_case();
	clean_tree(root);
	hfs_close(root);
	printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
