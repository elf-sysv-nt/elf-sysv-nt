/*
 * WP-41: elfsysv-exec, the Cygwin-side front end.
 *
 * An ordinary Cygwin program that runs another program through the branch this
 * package delivers. It is what stands in for the patch to Cygwin's own spawn
 * path while winsup is built elsewhere: the branch itself is a library, this
 * calls it exactly as the spawn path will, and the certification runs against
 * this rather than against a rebuilt cygwin1.dll.
 *
 * Given a file it classifies it, follows any `#!' chain, and either starts it
 * through the stub or reports that the host's own spawn keeps it. In the
 * second case it can run it that way too, with --host-fallback, so a `#!'
 * script whose interpreter is an ordinary program works from here as well as
 * an ELF one does. That is the half of the done-when that says scripts still
 * work: not that they are untouched, but that they go through the same
 * resolver and come out with the vector the kernel would have built.
 *
 * Usage:
 *   elfsysv-exec [options] FILE [ARG]...
 *
 * Options:
 *   -s PATH, --stub=PATH  The PE host stub. [default: $ELFSYSV_STUB]
 *   -f, --host-fallback   Run a non-ELF result through the host instead of
 *                         reporting it.
 *   -r, --report          Print what the resolver decided and exit without
 *                         running anything.
 *   -v, --verbose         Report each step.
 *   -V, --version         Print the version and exit.
 *   -h, --help            Print this message and exit.
 *
 * Exit: the program's status, 125 for a refusal, or 2 for usage.
 */
#include "dispatch.h"

#include <errno.h>
#include <sys/cygwin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char PROG[] = "elfsysv-exec";
static const char RELEASE[] = "elfsysv-exec 1.0";

static void usage(FILE *to)
{
	fprintf(to,
"Usage:\n"
"  elfsysv-exec [options] FILE [ARG]...\n"
"\n"
"Options:\n"
"  -s PATH, --stub=PATH  The PE host stub. [default: $ELFSYSV_STUB]\n"
"  -f, --host-fallback   Run a non-ELF result through the host.\n"
"  -r, --report          Print the resolver's decision and exit.\n"
"  -v, --verbose         Report each step.\n"
"  -V, --version         Print the version and exit.\n"
"  -h, --help            Print this message and exit.\n");
}

int main(int argc, char **argv)
{
	exec_config cfg;
	exec_spawned sp;
	exec_diag diag;
	exec_err rc;
	const char *stub = getenv("ELFSYSV_STUB");
	char stub_win[4096];
	int fallback = 0, report = 0, verbose = 0, status = 0, i;
	unsigned k;

	memset(&cfg, 0, sizeof cfg);
	memset(&diag, 0, sizeof diag);

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
		if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", RELEASE);
			return 0;
		}
		if (!strcmp(a, "-s") || !strcmp(a, "--stub")) {
			if (++i >= argc) { usage(stderr); return 2; }
			stub = argv[i];
		} else if (!strncmp(a, "--stub=", 7)) {
			stub = a + 7;
		} else if (!strcmp(a, "-f") || !strcmp(a, "--host-fallback")) {
			fallback = 1;
		} else if (!strcmp(a, "-r") || !strcmp(a, "--report")) {
			report = 1;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			verbose = 1;
		} else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "%s: unknown option %s\n", PROG, a);
			return 2;
		} else {
			break;
		}
	}
	if (i >= argc) {
		fprintf(stderr, "%s: nothing to run. Give a file.\n", PROG);
		return 2;
	}
	if (!stub || !*stub) {
		fprintf(stderr, "%s: no stub. Name one with --stub or "
			"ELFSYSV_STUB.\n", PROG);
		return 2;
	}

	/* CreateProcess is not a Cygwin call and knows nothing about mounts,
	 * so the stub is named to it the way the host names files. The path
	 * the user gave is a Cygwin one because everything else here is. */
	if (cygwin_conv_path(CCP_POSIX_TO_WIN_A | CCP_ABSOLUTE, stub,
			     stub_win, sizeof stub_win) != 0) {
		fprintf(stderr, "%s: cannot resolve the stub %s: %s\n",
			PROG, stub, strerror(errno));
		return 125;
	}

	cfg.stub = stub_win;
	/* Options for the stub itself ride on its command line, inserted ahead
	 * of the file, which is how the spawn path will carry them too. The
	 * WP-27 elfcall certification hands the stub its --runtime this way. */
	cfg.stub_options = getenv("ELFSYSV_STUB_OPTIONS");
	cfg.inherit = 1;

	rc = elf_exec(argv[i], argv + i, NULL, &cfg, &sp, &diag);

	if (rc != exec_ok && rc != exec_not_ours) {
		fprintf(stderr, "%s: %s: %s (%s)\n", PROG, exec_err_name(rc),
			diag.msg, diag.field ? diag.field : "?");
		return 125;
	}

	if (report || verbose) {
		printf("exec_verdict=%s\n", exec_err_name(rc));
		printf("exec_kind=%s\n", binfmt_kind_name(sp.resolved.kind));
		printf("exec_depth=%u\n", sp.resolved.depth);
		printf("exec_file=%s\n", sp.resolved.file);
		printf("exec_argc=%u\n", sp.resolved.argc);
		for (k = 0; k < sp.resolved.argc; k++)
			printf("exec_argv%u=%s\n", k, sp.resolved.argv[k]);
		if (rc == exec_ok)
			printf("exec_pid=%lu\n", sp.pid);
		if (report) {
			if (rc == exec_ok) {
				elf_exec_wait(&sp, &status);
				elf_exec_close(&sp);
			}
			return 0;
		}
	}

	if (rc == exec_not_ours) {
		if (!fallback) {
			fprintf(stderr, "%s: %s is the host's to run (%s)\n",
				PROG, sp.resolved.file,
				binfmt_kind_name(sp.resolved.kind));
			return 125;
		}
		/* The host path, entered with the vector the resolver rebuilt
		 * rather than the one this program was given. Cygwin's execv
		 * would repeat the `#!' walk from the original file; handing
		 * it the resolved file and vector is what makes the two paths
		 * agree about what a chain means. */
		execv(sp.resolved.file, (char *const *) sp.resolved.argv);
		fprintf(stderr, "%s: cannot run %s: %s\n", PROG,
			sp.resolved.file, strerror(errno));
		return 125;
	}

	if (elf_exec_wait(&sp, &status) != 0) {
		fprintf(stderr, "%s: lost the child\n", PROG);
		elf_exec_close(&sp);
		return 125;
	}
	elf_exec_close(&sp);
	return status;
}
