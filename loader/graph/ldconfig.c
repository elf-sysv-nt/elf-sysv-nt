/* WP-33: elf-ldconfig, the cache builder.
 *
 * It scans a list of directories for shared objects, reads each one's
 * DT_SONAME through WP-31's validated parser, and writes a cache mapping each
 * soname to the file that provides it, so the object graph can resolve a name
 * without walking the directories again. It is the analogue of ldconfig, and
 * the cache it writes is the format ldso_cache.h defines and DR-0011 explains.
 *
 * A soname found more than once keeps the file from the directory listed last,
 * so a directory later in the list takes precedence; the order is the caller's
 * to choose, and the tool does not guess a version preference.
 *
 * Usage:
 *   elf-ldconfig [options] [DIR...]
 *
 * Options:
 *   -o, --output FILE   write the cache here (env ELFSYSV_LDSO_CACHE)
 *                       [default: ./ld.so.cache]
 *   -f, --conf FILE     read newline-separated directories from FILE, scanned
 *                       before any DIR arguments
 *   -p, --print         read the --output cache and print its entries, exit
 *   -n, --dry-run       scan and report, but do not write the cache
 *   -v, --verbose       name each object as it is recorded
 *   -h, --help          print this message and exit
 *
 * Exit: 0 success, 1 an I/O or format error, 2 usage.
 */

#include "ldso_cache.h"
#include "../elf/elf_parse.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *usage =
"Usage:\n"
"  elf-ldconfig [options] [DIR...]\n"
"\n"
"Options:\n"
"  -o, --output FILE   cache to write (env ELFSYSV_LDSO_CACHE) [default: ./ld.so.cache]\n"
"  -f, --conf FILE     read newline-separated directories from FILE\n"
"  -p, --print         read the cache and print its entries, then exit\n"
"  -n, --dry-run       scan and report, but do not write\n"
"  -v, --verbose       name each object as it is recorded\n"
"  -h, --help          print this message and exit\n";

static unsigned char *read_whole(const char *path, size_t *sz)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
	long n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
	unsigned char *b = malloc((size_t)n ? (size_t)n : 1);
	if (!b) { fclose(f); return NULL; }
	size_t got = fread(b, 1, (size_t)n, f);
	fclose(f);
	if (got != (size_t)n) { free(b); return NULL; }
	*sz = (size_t)n;
	return b;
}

/* Record one file if it is a shared object with a DT_SONAME. Returns 1 if
 * recorded, 0 if skipped, -1 on a builder allocation failure. */
static int consider(ldso_cache_builder *b, const char *path, int verbose)
{
	size_t sz = 0;
	unsigned char *img = read_whole(path, &sz);
	if (!img) return 0;
	if (sz < 20 || img[0] != 0x7f || img[1] != 'E' ||
	    img[2] != 'L' || img[3] != 'F' || img[4] != 2) { free(img); return 0; }

	elf_parsed p; elf_diag d;
	if (elf_parse(img, sz, &p, &d) != elf_ok) { free(img); return 0; }
	/* A shared object without a soname is not addressable by name, so it is
	 * not something a DT_NEEDED can ask for; skip it as ldconfig does. */
	if (p.e_type != 3 /*ET_DYN*/ || !p.has_soname || !p.has_strtab) {
		free(img); return 0;
	}
	const char *soname = (const char *)img + p.strtab_off + p.soname;
	uint32_t flags = LDSO_CACHE_F_ELF64 |
	                 (p.e_machine == 62 ? LDSO_CACHE_F_X86_64 : 0);
	int rc = ldso_cache_builder_add(b, soname, path, flags);
	if (rc == 0 && verbose) printf("  %s -> %s\n", soname, path);
	free(img);
	return rc == 0 ? 1 : -1;
}

/* Scan one directory (non-recursively) and record every shared object in it.
 * Returns the number recorded, or -1 on a builder failure. A directory that
 * cannot be opened is reported and skipped, not fatal. */
static long scan_dir(ldso_cache_builder *b, const char *dir, int verbose)
{
	DIR *d = opendir(dir);
	if (!d) { fprintf(stderr, "elf-ldconfig: cannot open %s\n", dir); return 0; }
	long n = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		char path[4096];
		int k = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (k < 0 || (size_t)k >= sizeof path) continue;
		int r = consider(b, path, verbose);
		if (r < 0) { closedir(d); return -1; }
		n += r;
	}
	closedir(d);
	return n;
}

static int print_cache(const char *path)
{
	ldso_cache c; char m[128];
	ldso_cache_err e = ldso_cache_open(path, &c, m, sizeof m);
	if (e != ldso_cache_ok) {
		fprintf(stderr, "elf-ldconfig: %s: %s (%s)\n", path, m,
		        ldso_cache_err_name(e));
		return 1;
	}
	printf("%u libs found in cache `%s'\n", c.hdr->nentries, path);
	for (uint32_t i = 0; i < c.hdr->nentries; i++)
		printf("\t%s => %s\n", c.str + c.ent[i].soname_off,
		       c.str + c.ent[i].path_off);
	ldso_cache_close(&c);
	return 0;
}

/* Read newline-separated directories from a config file and scan each. Blank
 * lines and lines beginning with '#' are ignored. */
static long scan_conf(ldso_cache_builder *b, const char *conf, int verbose)
{
	FILE *f = fopen(conf, "r");
	if (!f) { fprintf(stderr, "elf-ldconfig: cannot open %s\n", conf); return 0; }
	long total = 0; char line[4096];
	while (fgets(line, sizeof line, f)) {
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		size_t l = strlen(s);
		while (l && (s[l-1] == '\n' || s[l-1] == '\r' ||
		             s[l-1] == ' ' || s[l-1] == '\t')) s[--l] = 0;
		if (!*s || *s == '#') continue;
		long n = scan_dir(b, s, verbose);
		if (n < 0) { fclose(f); return -1; }
		total += n;
	}
	fclose(f);
	return total;
}

int main(int argc, char **argv)
{
	const char *opt_output = NULL, *opt_conf = NULL;
	int do_print = 0, dry = 0, verbose = 0;
	const char *dirs[256]; size_t ndir = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		#define NEXT() (i + 1 < argc ? argv[++i] : (fputs(usage, stderr), exit(2), (char*)0))
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { fputs(usage, stdout); return 0; }
		else if (!strcmp(a, "-o") || !strcmp(a, "--output")) opt_output = NEXT();
		else if (!strcmp(a, "-f") || !strcmp(a, "--conf"))   opt_conf = NEXT();
		else if (!strcmp(a, "-p") || !strcmp(a, "--print"))  do_print = 1;
		else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) dry = 1;
		else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) verbose = 1;
		else if (a[0] == '-' && a[1]) { fprintf(stderr, "elf-ldconfig: unknown option %s\n", a);
		                                fputs(usage, stderr); return 2; }
		else if (ndir < 256) dirs[ndir++] = a;
		#undef NEXT
	}

	/* Precedence: option, then environment, then built-in default. */
	const char *env = getenv("ELFSYSV_LDSO_CACHE");
	const char *output = opt_output ? opt_output : (env ? env : "./ld.so.cache");

	if (do_print) return print_cache(output);

	ldso_cache_builder *b = ldso_cache_builder_new();
	if (!b) { fprintf(stderr, "elf-ldconfig: out of memory\n"); return 1; }

	long total = 0;
	if (opt_conf) {
		long n = scan_conf(b, opt_conf, verbose);
		if (n < 0) { ldso_cache_builder_free(b); goto oom; }
		total += n;
	}
	for (size_t i = 0; i < ndir; i++) {
		long n = scan_dir(b, dirs[i], verbose);
		if (n < 0) { ldso_cache_builder_free(b); goto oom; }
		total += n;
	}

	printf("elf-ldconfig: %ld object%s, %zu distinct soname%s\n",
	       total, total == 1 ? "" : "s",
	       ldso_cache_builder_count(b),
	       ldso_cache_builder_count(b) == 1 ? "" : "s");

	int rc = 0;
	if (!dry) {
		char m[128];
		if (ldso_cache_builder_write(b, output, m, sizeof m) != 0) {
			fprintf(stderr, "elf-ldconfig: writing %s: %s\n", output, m);
			rc = 1;
		} else if (verbose) {
			printf("elf-ldconfig: wrote %s\n", output);
		}
	}
	ldso_cache_builder_free(b);
	return rc;

oom:
	fprintf(stderr, "elf-ldconfig: out of memory building cache\n");
	return 1;
}
