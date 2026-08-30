/* WP-33 unit certification: walk the constructed graphs and hold each to a
 * written expectation, without a real ld.so. The differential in diff-ldso.sh
 * proves the order matches glibc; this proves the internals a differential
 * cannot see -- which search source resolved each name, which object is the
 * recorded loader, that a missing dependency is flagged rather than skipped,
 * and that the cache reader refuses a corrupt file rather than trusting it.
 *
 * Usage: graph_test GRAPHS_DIR CACHE_FILE
 * The graphs are built by mkgraph.sh; the cache is built over cacheonly/hidden
 * by elf-ldconfig before this runs. Exit 0 all passed, 1 a check failed.
 */

#include "../elf_graph.h"
#include "../ldso_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static const char *G;   /* graphs dir */

static void ok(int cond, const char *what)
{
	printf("  [%s] %s\n", cond ? "pass" : "FAIL", what);
	if (!cond) failures++;
}

/* Path of a graph member: GRAPHS_DIR/rel. Rotates among several buffers so two
 * live results -- a root path and an LD_LIBRARY_PATH, say -- do not alias. */
static const char *P(const char *rel)
{
	static char bufs[4][4096];
	static int i = 0;
	char *buf = bufs[i++ & 3];
	snprintf(buf, 4096, "%s/%s", G, rel);
	return buf;
}

/* Find a node by its DT_NEEDED name, or NULL. */
static const elf_graph_object *find(const elf_graph *g, const char *name)
{
	for (unsigned i = 0; i < g->count; i++)
		if (strcmp(g->obj[i].name, name) == 0) return &g->obj[i];
	return NULL;
}

/* True if the resolved path of the node named `name` contains `frag`. */
static int path_has(const elf_graph *g, const char *name, const char *frag)
{
	const elf_graph_object *o = find(g, name);
	return o && o->found && strstr(o->path, frag) != NULL;
}

static void test_diamond(void)
{
	printf("diamond: BFS order, shared dependency once\n");
	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	elf_graph g;
	int rc = elf_graph_build(P("diamond/main"), &cfg, &g);
	ok(rc == 0 && !g.error, "root walked");
	/* obj[0] root, then liba, libb, libd in breadth-first order. */
	ok(g.count == 4, "four nodes (root + three libraries)");
	ok(g.count >= 4 && strcmp(g.obj[1].name, "liba.so.0") == 0, "liba first");
	ok(g.count >= 4 && strcmp(g.obj[2].name, "libb.so.0") == 0, "libb second");
	ok(g.count >= 4 && strcmp(g.obj[3].name, "libd.so.0") == 0,
	   "libd last, after both parents");
	const elf_graph_object *d = find(&g, "libd.so.0");
	ok(d && d->found && d->source == elf_src_rpath, "libd via rpath");
	/* libd's recorded loader is liba (index 1), the first to introduce it. */
	ok(d && d->parent == 1, "libd's first loader is liba");
	elf_graph_free(&g);
}

static void test_precedence(void)
{
	printf("precedence: RPATH before, RUNPATH after LD_LIBRARY_PATH\n");
	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	cfg.ld_library_path = P("prec/lp");

	elf_graph g;
	elf_graph_build(P("prec/main_rpath"), &cfg, &g);
	ok(path_has(&g, "libpick.so.0", "/rp/"), "RPATH wins over LD_LIBRARY_PATH");
	const elf_graph_object *r = find(&g, "libpick.so.0");
	ok(r && r->source == elf_src_rpath, "resolved via rpath");
	elf_graph_free(&g);

	elf_graph_build(P("prec/main_runpath"), &cfg, &g);
	ok(path_has(&g, "libpick.so.0", "/lp/"), "LD_LIBRARY_PATH wins over RUNPATH");
	const elf_graph_object *u = find(&g, "libpick.so.0");
	ok(u && u->source == elf_src_ld_library_path, "resolved via LD_LIBRARY_PATH");
	elf_graph_free(&g);
}

static void test_inheritance(void)
{
	printf("inheritance: RPATH reaches a dependency's dependency, RUNPATH does not\n");
	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	elf_graph g;

	elf_graph_build(P("inherit/main_rpath"), &cfg, &g);
	ok(path_has(&g, "libt.so.0", "/deep/"),
	   "RPATH inherited: libt found through the root's rpath");
	ok(g.missing_count == 0, "nothing missing in the rpath graph");
	elf_graph_free(&g);

	elf_graph_build(P("inherit/main_runpath"), &cfg, &g);
	const elf_graph_object *t = find(&g, "libt.so.0");
	ok(t && !t->found, "RUNPATH not inherited: libt not found");
	ok(g.missing_count == 1, "exactly one missing dependency");
	/* liba itself resolved -- only its own dependency is unreachable. */
	ok(path_has(&g, "liba.so.0", "inherit/"), "liba still resolved");
	elf_graph_free(&g);
}

static void test_origin(void)
{
	printf("origin: $ORIGIN expands to the object's directory\n");
	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	elf_graph g;
	elf_graph_build(P("origin/main"), &cfg, &g);
	ok(path_has(&g, "libplug.so.0", "/plug/"), "libplug found under $ORIGIN/plug");
	elf_graph_free(&g);
}

static void test_missing(void)
{
	printf("missing: an unresolved NEEDED is a flagged node, not a gap\n");
	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	elf_graph g;
	elf_graph_build(P("missing/main"), &cfg, &g);
	ok(path_has(&g, "libpresent.so.0", "missing/"), "libpresent resolved");
	const elf_graph_object *ghost = find(&g, "libghost.so.0");
	ok(ghost && !ghost->found, "libghost present as a not-found node");
	ok(g.missing_count == 1, "one missing dependency counted");
	elf_graph_free(&g);
}

static void test_cache(const char *cache_file)
{
	printf("cache: a name reachable only through the cache resolves via it\n");
	/* Without a cache, cacheonly/main cannot find libcache.so.0 at all. */
	elf_graph_config bare; memset(&bare, 0, sizeof bare);
	elf_graph g;
	elf_graph_build(P("cacheonly/main"), &bare, &g);
	const elf_graph_object *c0 = find(&g, "libcache.so.0");
	ok(c0 && !c0->found, "without a cache, libcache is not found");
	elf_graph_free(&g);

	/* With the cache elf-ldconfig built over cacheonly/hidden, it resolves. */
	ldso_cache cache; char m[128];
	ldso_cache_err e = ldso_cache_open(cache_file, &cache, m, sizeof m);
	ok(e == ldso_cache_ok, "cache file opens and validates");
	if (e != ldso_cache_ok) { printf("    (%s)\n", m); return; }

	ok(ldso_cache_lookup(&cache, "libcache.so.0", 0) != NULL,
	   "cache lookup finds the soname");
	ok(ldso_cache_lookup(&cache, "libnope.so.0", 0) == NULL,
	   "cache lookup misses an absent soname");

	elf_graph_config cfg; memset(&cfg, 0, sizeof cfg);
	cfg.cache = &cache;
	elf_graph_build(P("cacheonly/main"), &cfg, &g);
	const elf_graph_object *c1 = find(&g, "libcache.so.0");
	ok(c1 && c1->found && c1->source == elf_src_cache,
	   "with the cache, libcache resolves via the cache");
	ok(c1 && strstr(c1->path, "hidden") != NULL, "to the hidden directory");
	elf_graph_free(&g);
	ldso_cache_close(&cache);
}

static void test_cache_reader(void)
{
	printf("cache reader: a corrupt cache is refused, not trusted\n");
	ldso_cache_builder *b = ldso_cache_builder_new();
	ldso_cache_builder_add(b, "libx.so.0", "/lib/libx.so.0", LDSO_CACHE_F_ELF64);
	unsigned char *buf = NULL; size_t n = 0;
	ldso_cache_builder_serialize(b, &buf, &n);
	ldso_cache_builder_free(b);

	const char *tmp = "./.wp33_corrupt.cache";
	/* A good buffer opens. */
	FILE *f = fopen(tmp, "wb"); fwrite(buf, 1, n, f); fclose(f);
	ldso_cache c; char m[128];
	ok(ldso_cache_open(tmp, &c, m, sizeof m) == ldso_cache_ok, "valid cache opens");
	ldso_cache_close(&c);
	/* A flipped magic byte is rejected. */
	buf[0] ^= 0xff;
	f = fopen(tmp, "wb"); fwrite(buf, 1, n, f); fclose(f);
	ok(ldso_cache_open(tmp, &c, m, sizeof m) == ldso_cache_err_magic,
	   "flipped magic rejected");
	/* A truncated header is rejected. */
	f = fopen(tmp, "wb"); fwrite(buf, 1, 4, f); fclose(f);
	ok(ldso_cache_open(tmp, &c, m, sizeof m) == ldso_cache_err_size,
	   "truncated file rejected");
	remove(tmp);
	free(buf);
}

static void test_tokens(void)
{
	printf("token expansion: $ORIGIN, $LIB, $PLATFORM and their braced forms\n");
	char out[256];
	elf_graph_expand_tokens("$ORIGIN/lib", "/a/b", "lib64", "x86_64",
	                        out, sizeof out);
	ok(strcmp(out, "/a/b/lib") == 0, "$ORIGIN");
	elf_graph_expand_tokens("/usr/${LIB}/x", "/a", "lib64", "x86_64",
	                        out, sizeof out);
	ok(strcmp(out, "/usr/lib64/x") == 0, "${LIB}");
	elf_graph_expand_tokens("p/$PLATFORM", "/a", "lib64", "x86_64",
	                        out, sizeof out);
	ok(strcmp(out, "p/x86_64") == 0, "$PLATFORM");
	elf_graph_expand_tokens("$UNKNOWN/x", "/a", "lib64", "x86_64",
	                        out, sizeof out);
	ok(strcmp(out, "$UNKNOWN/x") == 0, "an unknown token is left literal");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: graph_test GRAPHS_DIR CACHE_FILE\n");
		return 2;
	}
	G = argv[1];
	const char *cache_file = argv[2];

	test_tokens();
	test_diamond();
	test_precedence();
	test_inheritance();
	test_origin();
	test_missing();
	test_cache(cache_file);
	test_cache_reader();

	printf("\n%s: %d check%s failed\n", failures ? "FAIL" : "ok",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
