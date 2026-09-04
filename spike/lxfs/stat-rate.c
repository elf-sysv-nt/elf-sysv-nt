/* The Cygwin half of q8's comparison, which cannot live in the native probe:
 * a mingw process has no Cygwin stat() to call, and calling the Win32
 * GetFileAttributesEx instead would measure something nobody uses. So this is
 * built by Cygwin's own gcc against Cygwin's libc, stats the same tree the
 * probe built, and prints the same shape of key=value line.
 *
 * The number it produces is context and never a finding: it moves with the
 * cache, the volume and whatever else the machine is doing.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define RELEASE "stat-rate 1.0"

static double now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec * 1e9 + (double) ts.tv_nsec;
}

int main(int argc, char **argv)
{
	char path[4096];
	struct stat sb;
	const char *dir = NULL;
	int count = 2000, i, ok = 0;
	double t0, t1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-V") == 0 ||
		    strcmp(argv[i], "--version") == 0) {
			printf("%s\n", RELEASE);
			return 0;
		}
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
			count = atoi(argv[++i]);
		else if (strncmp(argv[i], "--count=", 8) == 0)
			count = atoi(argv[i] + 8);
		else if (argv[i][0] != '-')
			dir = argv[i];
	}
	if (dir == NULL || count < 1) {
		fprintf(stderr, "usage: stat-rate [-n N] <directory>\n");
		return 2;
	}

	/* Warm pass, then the measured one, so both sides of the comparison
	 * are warm-cache rates rather than one cold and one warm. */
	for (i = 0; i < count; i++) {
		snprintf(path, sizeof path, "%s/f%06d", dir, i);
		stat(path, &sb);
	}
	t0 = now_ns();
	for (i = 0; i < count; i++) {
		snprintf(path, sizeof path, "%s/f%06d", dir, i);
		if (stat(path, &sb) == 0)
			ok++;
	}
	t1 = now_ns();

	printf("q8_cygwin_files=%d\n", count);
	printf("q8_cygwin_ok=%d\n", ok);
	printf("q8_cygwin_ns_per_stat=%.0f\n", (t1 - t0) / count);
	return 0;
}
