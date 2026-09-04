/* What Cygwin's fork() costs on this machine, for the number that sits beside
 * the clone's.
 *
 * Built with the Cygwin gcc rather than the cross compiler, because the thing
 * being priced is Cygwin's fork and there is no other way to call it. It is
 * the only part of this spike that is not native, and it decides nothing: the
 * verdict in measure.sh never reads these keys.
 *
 * The timed region is the fork call as the parent sees it. The child exits
 * immediately and is reaped outside the region, so what is measured is the
 * same shape as the clone's: the cost of producing a second address space,
 * not the cost of tearing it down. It is still not like for like -- Cygwin's
 * fork hands back a process with a running thread and a re-established heap,
 * where the clone hands back an address space with nobody in it -- and
 * README.md says so where the numbers are read.
 *
 * Usage:
 *   fork-timer [options]
 *
 * Options:
 *   -n N, --iterations=N  Forks to time. [default: 200]
 *   -V, --version         Print the version and exit.
 *   -h, --help            Print this message and exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define RELEASE "fork-timer 1.0"

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *) a, y = *(const double *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static double now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec * 1e6 + (double) ts.tv_nsec / 1e3;
}

int main(int argc, char **argv)
{
	int n = 200, i, kept = 0, failures = 0;
	double *us, median = 0.0;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			printf("Usage:\n  fork-timer [options]\n\n"
			       "Options:\n"
			       "  -n N, --iterations=N  Forks to time. [default: 200]\n"
			       "  -V, --version         Print the version and exit.\n"
			       "  -h, --help            Print this message and exit.\n");
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", RELEASE);
			return 0;
		} else if (!strcmp(a, "-n") && i + 1 < argc) {
			n = atoi(argv[++i]);
		} else if (!strncmp(a, "--iterations=", 13)) {
			n = atoi(a + 13);
		} else {
			fprintf(stderr, "fork-timer: unknown option %s\n", a);
			return 2;
		}
	}
	if (n < 1)
		n = 1;

	us = malloc((size_t) n * sizeof *us);
	if (!us) {
		fprintf(stderr, "fork-timer: out of memory\n");
		return 1;
	}

	for (i = 0; i < n; i++) {
		double t0 = now_us(), t1;
		pid_t pid = fork();
		if (pid == 0)
			_exit(0);
		t1 = now_us();
		if (pid < 0) {
			failures++;
			continue;
		}
		us[kept++] = t1 - t0;
		while (waitpid(pid, NULL, 0) < 0)
			;
	}

	if (kept > 0) {
		qsort(us, (size_t) kept, sizeof *us, cmp_double);
		median = us[kept / 2];
	}
	free(us);

	printf("q7_cygwin_fork_iterations=%d\n", n);
	printf("q7_cygwin_fork_failures=%d\n", failures);
	printf("q7_cygwin_fork_median_us=%.1f\n", median);
	return 0;
}
