/*
 * main.c -- lk-host, the Linux-personality core running one static ELF on the
 * substrate.  It reads the program, maps it, builds its initial stack with the
 * gate published in AT_SYSINFO, enters it on a substrate thread, and waits for
 * exit_group to hand back the code the process then exits with.
 *
 * The command line follows the house CLI conventions: a usage block that is the
 * parser's contract, the standard --help/--version/--verbose/--quiet set, one
 * option that also reads an environment variable, results on stdout and
 * diagnostics on stderr, and exit 2 for a usage error.  On a good run the exit
 * code is the program's own, the way env or timeout pass a child's status up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "substrate.h"
#include "binfmt_elf.h"
#include "exec.h"
#include "gate.h"
#include "host.h"
#include "task.h"
#include "vma.h"

#define PROG		"lk-host"
#define VERSION		"lk-host (elf-sysv-nt core) 1.0"
#define TID_MAIN	1000
#define KSTACK_SIZE	(256 * 1024)

static const char usage_text[] =
"Usage:\n"
"  lk-host [options] ELF [ARG...]\n"
"  lk-host -h | --help\n"
"  lk-host -V | --version\n"
"\n"
"Run a static ELF on substrate N through the syscall gate.\n"
"\n"
"Options:\n"
"  -v, --verbose      Trace load and entry steps to stderr.\n"
"  -q, --quiet        Suppress diagnostics; only the program's output remains.\n"
"      --timeout=MS   Give up if the run has not finished in MS milliseconds\n"
"                     [default: 10000].  Also LK_HOST_TIMEOUT.\n"
"      --root=DIR     The host directory that is the Linux root file system\n"
"                     [default: the ELF's directory].  Also LK_HOST_ROOT.\n"
"      --exe=PATH     The Linux path the program is known by, for\n"
"                     /proc/self/exe and maps [default: /<ELF's base name>].\n"
"  -h, --help         Print this message and exit.\n"
"  -V, --version      Print the version and exit.\n"
"\n"
"Exit: the program's exit_group code on a completed run; 1 if the ELF cannot\n"
"be loaded or run, 2 for a usage error, 3 if the run times out.\n";

static void usage(FILE *f) { fputs(usage_text, f); }

static int verbose, quiet;

static void trace(const char *msg)
{
	if (verbose && !quiet)
		fprintf(stderr, "%s: %s\n", PROG, msg);
}

static void diag(const char *msg)
{
	if (!quiet)
		fprintf(stderr, "%s: %s\n", PROG, msg);
}

/* Read the whole file at path into a fresh buffer; *len gets its size. */
static void *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *buf;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	buf = malloc((size_t)n ? (size_t)n : 1);
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf)
		*len = (size_t)n;
	return buf;
}

int main(int argc, char **argv)
{
	const char *timeout_env = getenv("LK_HOST_TIMEOUT");
	unsigned timeout = timeout_env ? (unsigned)strtoul(timeout_env, NULL, 10)
				       : 10000;
	const char *elf_path = NULL;
	const char *root_env = getenv("LK_HOST_ROOT");
	const char *root_dir = root_env ? root_env : NULL;
	const char *exe_name = NULL;
	char root_buf[4096], exe_buf[4096];
	char **prog_argv;
	int prog_argc, i = 1, code;
	size_t len = 0;
	void *image, *kstack;
	struct substrate *s;
	struct load_info li;

	/* Options up to the first non-option or "--"; the option layer wins over
	 * the environment for the timeout. */
	for (; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "--")) { i++; break; }
		if (a[0] != '-' || a[1] == '\0') break;
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout); return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			puts(VERSION); return 0;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			verbose = 1;
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			quiet = 1;
		} else if (!strncmp(a, "--timeout=", 10)) {
			timeout = (unsigned)strtoul(a + 10, NULL, 10);
		} else if (!strcmp(a, "--timeout")) {
			if (++i >= argc) { usage(stderr); return 2; }
			timeout = (unsigned)strtoul(argv[i], NULL, 10);
		} else if (!strncmp(a, "--root=", 7)) {
			root_dir = a + 7;
		} else if (!strcmp(a, "--root")) {
			if (++i >= argc) { usage(stderr); return 2; }
			root_dir = argv[i];
		} else if (!strncmp(a, "--exe=", 6)) {
			exe_name = a + 6;
		} else {
			fprintf(stderr, "%s: unknown option %s\n", PROG, a);
			usage(stderr);
			return 2;
		}
	}
	if (i >= argc) { usage(stderr); return 2; }

	elf_path = argv[i];
	prog_argv = &argv[i];		/* argv[0] is the program itself */
	prog_argc = argc - i;

	image = read_file(elf_path, &len);
	if (!image) {
		diag("cannot read the ELF file");
		return 1;
	}
	if (!root_dir) {
		/* the ELF's own directory, in the host's spelling */
		const char *sl = strrchr(elf_path, '\\');
		const char *sl2 = strrchr(elf_path, '/');
		if (sl2 > sl) sl = sl2;
		if (sl) {
			size_t n = (size_t)(sl - elf_path);
			if (n == 0) n = 1;
			if (n >= sizeof root_buf) n = sizeof root_buf - 1;
			memcpy(root_buf, elf_path, n);
			root_buf[n] = 0;
		} else {
			strcpy(root_buf, ".");
		}
		root_dir = root_buf;
	}
	if (!exe_name) {
		const char *sl = strrchr(elf_path, '\\');
		const char *sl2 = strrchr(elf_path, '/');
		if (sl2 > sl) sl = sl2;
		snprintf(exe_buf, sizeof exe_buf, "/%s", sl ? sl + 1 : elf_path);
		exe_name = exe_buf;
	}
	if (task_init(root_dir) != 0) {
		diag("cannot open the root directory");
		return 1;
	}
	vfs_set_exe_path(exe_name);
	elf_set_image_name(exe_name);
	vma_reset();

	s = substrate_create();
	kstack = host_alloc_kstack(KSTACK_SIZE);
	if (!s || !kstack) {
		diag("out of resources bringing up the substrate");
		return 1;
	}
	host_run_init();
	gate_init(s, TID_MAIN, kstack);

	trace("loading the ELF");
	if (elf_load(s, image, len, &li) != 0) {
		diag("not a static x86-64 ELF this core can load");
		return 1;
	}

	trace("building the initial stack and entering");
	if (exec_enter(s, &li, (uint64_t)(uintptr_t)&gate_entry,
		       prog_argc, prog_argv, TID_MAIN) != 0) {
		diag("could not build the process image or start the thread");
		return 1;
	}

	code = host_run_wait(timeout);
	if (code < 0) {
		diag("the run did not finish within the timeout");
		return 3;
	}
	trace("run finished");
	return code;
}
