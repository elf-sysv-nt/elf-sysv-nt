/*
 * WP-56 item 1 file stage: rp_slurp reads the image host-safe, with no libc
 * call and so no ABI crossing, so it is certified here natively -- no faced
 * runtime -- against files written on the spot. Built with -DELFSYSV_REALPROC
 * so realproc-file.c compiles; it uses Win32 (CreateFileA/ReadFile), which the
 * native toolchain links like any other program.
 */
#ifndef ELFSYSV_REALPROC
#define ELFSYSV_REALPROC
#endif
#include "../realproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("FAIL %s\n", what); fails++; }
}

/* Write bytes to a path with the plain libc (the test harness, not the code
 * under test), so rp_slurp is read back against a known content. */
static int put(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	if (n && fwrite(data, 1, n, f) != n) { fclose(f); return 0; }
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[1024];
	size_t size = 0;
	int err;
	unsigned char *buf;

	/* A body with embedded NULs and high bytes, the shape of an ELF image. */
	static const unsigned char body[] = {
		0x7f, 'E', 'L', 'F', 0x00, 0x01, 0xff, 0x00,
		'a', 'b', 'c', 0x00, 0x00, 0xde, 0xad, 0xbe
	};

	snprintf(path, sizeof path, "%s/rp-file-body.bin", dir);
	ck(put(path, body, sizeof body), "write body");
	err = -1;
	buf = rp_slurp(path, &size, &err);
	ck(buf != NULL, "slurp body non-null");
	ck(size == sizeof body, "slurp body size");
	ck(buf && memcmp(buf, body, sizeof body) == 0, "slurp body bytes");

	/* An empty file: size 0, a non-NULL buffer (slurp allocates at least 1). */
	snprintf(path, sizeof path, "%s/rp-file-empty.bin", dir);
	ck(put(path, "", 0), "write empty");
	size = 123; err = -1;
	buf = rp_slurp(path, &size, &err);
	ck(buf != NULL, "slurp empty non-null");
	ck(size == 0, "slurp empty size");

	/* A missing file: NULL, err == RP_SLURP_OPEN, the "cannot read" case. */
	snprintf(path, sizeof path, "%s/rp-file-absent.bin", dir);
	remove(path);
	err = -1;
	buf = rp_slurp(path, &size, &err);
	ck(buf == NULL, "slurp absent null");
	ck(err == RP_SLURP_OPEN, "slurp absent err=open");

	if (fails) { printf("file: %d FAILED\n", fails); return 1; }
	printf("file: rp_slurp OK\n");
	return 0;
}
