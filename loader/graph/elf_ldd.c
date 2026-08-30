/* WP-33: elf-ldd, the ldd-equivalent over the object graph.
 *
 * It walks the graph rooted at an ELF file and prints the objects that would
 * be loaded, in load order, the way ld.so's own LD_TRACE_LOADED_OBJECTS does:
 * the vDSO placeholder, each dependency as `name => path`, a missing one as
 * `name => not found`, and the interpreter last. The addresses are zeros
 * because nothing is mapped here; the order is the whole point, and it is the
 * order the differential test holds against a real ld.so.
 *
 * Usage:
 *   elf-ldd [options] FILE
 *
 * Options:
 *   -L, --library-path LIST   colon-separated search dirs; overrides the
 *                             LD_LIBRARY_PATH environment variable
 *   -c, --cache FILE          consult this ldconfig cache
 *   -s, --default-path LIST   system default dirs [default: /lib64:/usr/lib64]
 *   -o, --origin DIR          override $ORIGIN for the root object
 *       --lib TOKEN           value for $LIB [default: lib64]
 *       --platform TOKEN      value for $PLATFORM [default: x86_64]
 *       --source              annotate each object with the search source
 *       --bare                one `name => target` per object, no vDSO,
 *                             interpreter, or addresses; the canonical form
 *                             the differential compares
 *   -h, --help                print this message and exit
 *
 * Exit: 0 the root was walked, 1 the root could not be read or parsed, 2 usage.
 */

#include "elf_graph.h"
#include "ldso_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *usage =
"Usage:\n"
"  elf-ldd [options] FILE\n"
"\n"
"Options:\n"
"  -L, --library-path LIST   colon-separated search dirs (overrides env)\n"
"  -c, --cache FILE          consult this ldconfig cache\n"
"  -s, --default-path LIST   system default dirs [default: /lib64:/usr/lib64]\n"
"  -o, --origin DIR          override $ORIGIN for the root object\n"
"      --lib TOKEN           value for $LIB [default: lib64]\n"
"      --platform TOKEN      value for $PLATFORM [default: x86_64]\n"
"      --source              annotate each object with the search source\n"
"      --bare                canonical `name => target` per object\n"
"  -h, --help                print this message and exit\n";

static const char *zero = "0x0000000000000000";

/* Split a colon list into a NULL-terminated argv-style array (for the default
 * path). The returned array and its strings are one allocation the caller
 * frees. Returns NULL on allocation failure or an empty list. */
static char **split_default(const char *list, size_t *count, char **bufout)
{
	size_t n = 1;
	for (const char *p = list; *p; p++) if (*p == ':') n++;
	char **v = calloc(n + 1, sizeof *v);
	char *buf = strdup(list);
	if (!v || !buf) { free(v); free(buf); return NULL; }
	size_t k = 0;
	char *save = NULL, *tok = strtok_r(buf, ":", &save);
	while (tok) { v[k++] = tok; tok = strtok_r(NULL, ":", &save); }
	*count = k;
	*bufout = buf;   /* free *bufout and the array when done */
	return v;
}

int main(int argc, char **argv)
{
	const char *file = NULL;
	const char *opt_libpath = NULL, *opt_cache = NULL, *opt_origin = NULL;
	const char *opt_default = "/lib64:/usr/lib64";
	const char *opt_lib = "lib64", *opt_platform = "x86_64";
	int bare = 0, show_source = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		#define NEXT() (i + 1 < argc ? argv[++i] : (fputs(usage, stderr), exit(2), (char*)0))
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { fputs(usage, stdout); return 0; }
		else if (!strcmp(a, "-L") || !strcmp(a, "--library-path")) opt_libpath = NEXT();
		else if (!strcmp(a, "-c") || !strcmp(a, "--cache"))        opt_cache = NEXT();
		else if (!strcmp(a, "-s") || !strcmp(a, "--default-path")) opt_default = NEXT();
		else if (!strcmp(a, "-o") || !strcmp(a, "--origin"))       opt_origin = NEXT();
		else if (!strcmp(a, "--lib"))       opt_lib = NEXT();
		else if (!strcmp(a, "--platform"))  opt_platform = NEXT();
		else if (!strcmp(a, "--source"))    show_source = 1;
		else if (!strcmp(a, "--bare"))      bare = 1;
		else if (a[0] == '-' && a[1]) { fprintf(stderr, "elf-ldd: unknown option %s\n", a);
		                                fputs(usage, stderr); return 2; }
		else if (!file) file = a;
		else { fputs(usage, stderr); return 2; }
		#undef NEXT
	}
	if (!file) { fputs(usage, stderr); return 2; }

	/* Precedence: option beats environment beats built-in default. */
	const char *libpath = opt_libpath ? opt_libpath : getenv("LD_LIBRARY_PATH");

	ldso_cache cache; memset(&cache, 0, sizeof cache);
	int have_cache = 0;
	if (opt_cache) {
		char m[128];
		ldso_cache_err e = ldso_cache_open(opt_cache, &cache, m, sizeof m);
		if (e != ldso_cache_ok) {
			fprintf(stderr, "elf-ldd: cache %s: %s (%s)\n", opt_cache,
			        m, ldso_cache_err_name(e));
			return 2;
		}
		have_cache = 1;
	}

	size_t dcount = 0; char *dbuf = NULL;
	char **dpaths = split_default(opt_default, &dcount, &dbuf);

	elf_graph_config cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.ld_library_path = libpath;
	cfg.default_paths   = (const char *const *)dpaths;
	cfg.default_count   = dcount;
	cfg.cache           = have_cache ? &cache : NULL;
	cfg.want_flags      = 0;
	cfg.origin_override = opt_origin;
	cfg.lib_token       = opt_lib;
	cfg.platform_token  = opt_platform;

	elf_graph g;
	int rc = elf_graph_build(file, &cfg, &g);
	if (rc != 0 || g.error) {
		fprintf(stderr, "elf-ldd: %s: %s\n", file,
		        g.errmsg[0] ? g.errmsg : "cannot process object");
		elf_graph_free(&g);
		if (have_cache) ldso_cache_close(&cache);
		free(dbuf); free(dpaths);
		return 1;
	}

	/* obj[0] is the root program itself, which ld.so does not list. The
	 * dependencies are obj[1..], in load order. ld.so's trace defers every
	 * "not found" line to the very end, after the interpreter, so this walks
	 * the found objects first and the missing ones after -- reproducing ldd's
	 * order rather than the load map's, which the graph itself still holds. */
	if (bare) {
		for (int pass = 0; pass < 2; pass++)
			for (unsigned i = 1; i < g.count; i++) {
				const elf_graph_object *o = &g.obj[i];
				if (o->found != (pass == 0)) continue;
				/* Full resolved path so a precedence test can see which
				 * directory won; the differential strips the namespace
				 * prefix so /c/... and /mnt/c/... compare equal. */
				printf("%s => %s", o->name, o->found ? o->path : "not found");
				if (show_source) printf("\t[%s]", elf_graph_source_name(o->source));
				putchar('\n');
			}
	} else {
		printf("\tlinux-vdso.so.1 (%s)\n", zero);
		for (unsigned i = 1; i < g.count; i++) {
			const elf_graph_object *o = &g.obj[i];
			if (!o->found) continue;
			printf("\t%s => %s (%s)", o->name, o->path, zero);
			if (show_source) printf("\t[%s]", elf_graph_source_name(o->source));
			putchar('\n');
		}
		if (g.has_interp)
			printf("\t%s (%s)\n", g.interp, zero);
		for (unsigned i = 1; i < g.count; i++) {
			const elf_graph_object *o = &g.obj[i];
			if (o->found) continue;
			printf("\t%s => not found", o->name);
			if (show_source) printf("\t[%s]", elf_graph_source_name(o->source));
			putchar('\n');
		}
	}

	int missing = (int)g.missing_count;
	elf_graph_free(&g);
	if (have_cache) ldso_cache_close(&cache);
	free(dbuf); free(dpaths);
	(void)missing;   /* ldd prints missing lines but still exits 0 */
	return 0;
}
