/* elfsysv-fork -- the front end the spawn path will be.
 *
 * winsup is not in this tree, so the three phases are certified through a
 * program that calls them exactly as Cygwin's fork will: prepare, then the
 * host's fork, then parent on one side and child on the other, with the
 * manifest packed between prepare and the call and read in the child. WP-41
 * certified its branch the same way and for the same reason.
 *
 * Usage:
 *   elfsysv-fork [options]
 *
 * Options:
 *   -f, --flavor F   fork, vfork or spawn (default fork).
 *   -n, --count N    forks to perform (default 1).
 *   -r, --regions N  reservations to take before forking (default 2).
 *   -q, --quiet      Errors only.
 *   -h, --help       Print this message and exit.
 *
 * Exit: 0 every child crossed intact, 1 a child did not, 2 usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fork.h"

static elf_fork_state fs;
static int quiet;

/* The manifest is packed once, before the fork, into memory the child inherits
 * as a copy. In Cygwin's fork this is the child_info block the parent writes
 * into the child; here it is a plain buffer, which crosses by the same copy. */
static unsigned char packed[4096];
static size_t packed_len;

static int prepare_ran, parent_ran, child_ran;

static void h_prepare(void) { prepare_ran++; }
static void h_parent(void) { parent_ran++; }
static void h_child(void) { child_ran++; }

static void usage(void)
{
	fputs("Usage:\n"
	      "  elfsysv-fork [options]\n\n"
	      "Options:\n"
	      "  -f, --flavor F   fork, vfork or spawn (default fork).\n"
	      "  -n, --count N    forks to perform (default 1).\n"
	      "  -r, --regions N  reservations to take (default 2).\n"
	      "  -q, --quiet      Errors only.\n"
	      "  -h, --help       Print this message and exit.\n", stdout);
}

/* Reservations the host's fork does not replay, taken the way WP-41's window is
 * taken: raw, at an address of the host's choosing, then recorded. */
static int take_regions(int n)
{
	const elf_fork_mem *m = elf_fork_mem_host();
	for (int i = 0; i < n; i++) {
		/* Ask the host where, by reserving through the same path with a base
		 * of zero is not available here, so a fixed high window is used: it is
		 * above where any Cygwin allocation lands and below the 47-bit ceiling. */
		uint64_t base = UINT64_C(0x60000000) + (uint64_t)i * 0x100000;
		if (m->reserve(m->ctx, base, 0x10000) != 0) {
			fprintf(stderr, "elfsysv-fork: cannot reserve 0x%llx\n",
			        (unsigned long long)base);
			return -1;
		}
		if (elf_fork_region_add(&fs, base, 0x10000, elf_fork_region_reserve,
		                        0, "test window") != 0) {
			fprintf(stderr, "elfsysv-fork: %s\n", fs.why);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	elf_fork_flavor flavor = elf_fork_flavor_fork;
	int count = 1, regions = 2;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
		else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) quiet = 1;
		else if ((!strcmp(a, "-n") || !strcmp(a, "--count")) && i + 1 < argc)
			count = atoi(argv[++i]);
		else if ((!strcmp(a, "-r") || !strcmp(a, "--regions")) && i + 1 < argc)
			regions = atoi(argv[++i]);
		else if ((!strcmp(a, "-f") || !strcmp(a, "--flavor")) && i + 1 < argc) {
			const char *f = argv[++i];
			if (!strcmp(f, "fork")) flavor = elf_fork_flavor_fork;
			else if (!strcmp(f, "vfork")) flavor = elf_fork_flavor_vfork;
			else if (!strcmp(f, "spawn")) flavor = elf_fork_flavor_posix_spawn;
			else { usage(); return 2; }
		} else { usage(); return 2; }
	}
	if (count < 1 || regions < 0 || regions > ELF_FORK_REGION_MAX) {
		usage();
		return 2;
	}

	elf_fork_state_init(&fs, NULL, NULL, NULL, NULL, NULL);
	if (elf_fork_atfork(&fs, h_prepare, h_parent, h_child) != 0) {
		fprintf(stderr, "elfsysv-fork: %s\n", fs.why);
		return 1;
	}
	if (take_regions(regions) != 0)
		return 1;

	for (int i = 0; i < count; i++) {
		if (elf_fork_prepare(&fs, flavor) != 0) {
			fprintf(stderr, "elfsysv-fork: %s\n", fs.why);
			return 1;
		}
		if (elf_fork_manifest_pack(&fs, packed, sizeof packed, &packed_len) != 0) {
			fprintf(stderr, "elfsysv-fork: the manifest does not fit\n");
			return 1;
		}

		/* vfork and posix_spawn take the same three phases; the host call
		 * beneath them is Cygwin's fork in every case, so the front end makes
		 * the one call and the flavour travels only as a label. */
		pid_t pid = fork();
		if (pid < 0) {
			elf_fork_parent(&fs);
			perror("elfsysv-fork: fork");
			return 1;
		}

		if (pid == 0) {
			int rc = elf_fork_child(&fs, packed, packed_len);
			if (rc != 0) {
				fprintf(stderr, "elfsysv-fork: child: %s\n", fs.why);
				_exit(1);
			}
			if (child_ran != 1) {
				fprintf(stderr, "elfsysv-fork: child handler ran %d times\n",
				        child_ran);
				_exit(1);
			}
			_exit(0);
		}

		elf_fork_parent(&fs);

		int status = 0;
		if (waitpid(pid, &status, 0) != pid) {
			perror("elfsysv-fork: waitpid");
			return 1;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "elfsysv-fork: child %d did not cross intact\n", i);
			return 1;
		}
	}

	if (prepare_ran != count || parent_ran != count) {
		fprintf(stderr, "elfsysv-fork: handlers ran %d/%d times, wanted %d\n",
		        prepare_ran, parent_ran, count);
		return 1;
	}

	if (!quiet)
		printf("elfsysv-fork: %d %s, %d regions, every child intact\n",
		       count, count == 1 ? "fork" : "forks", regions);
	return 0;
}
