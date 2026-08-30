/* WP-35 certification harness.
 *
 * Two modes. `unit` holds the hash tables and the binding rule to synthetic
 * inputs the loader controls exactly: the SysV and GNU probes find a present
 * name and reject an absent one, the GNU Bloom filter rejects without walking a
 * chain, the scope walk takes the first global and lets it override an earlier
 * weak, an only-weak name resolves to the first weak, interposition follows
 * scope order, and the version-matcher seam skips a candidate a matcher rejects.
 *
 * `collide` is the differential's own side: it walks the three-way collision
 * graph with WP-33, maps every object with WP-32, discovers each object's
 * dynamic view with WP-34, builds the global scope in canonical order with the
 * preload objects interposed after the root, resolves collide(), and -- because
 * collide() is a leaf returning a constant -- calls the resolved pointer to
 * read the winning tag. It prints "winner=<soname> tag=<n>", which diff-ldso.sh
 * holds against what a real ld.so does with the same graph.
 */
#define _GNU_SOURCE
#include "../elf_lookup.h"
#include "../../elf/elf_parse.h"
#include "../../map/elf_map.h"
#include "../../graph/elf_graph.h"
#include "../../reloc/elf_reloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libgen.h>

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-56s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

/* ---- synthetic objects for the unit mode ------------------------------- */

#define MAXSYM 32
struct synth {
	Elf64_Sym sym[MAXSYM];
	char      str[512];
	uint32_t  strn;
	uint32_t  nsym;
	uint32_t  sysv[2 + 2 * MAXSYM];
	uint64_t  gnu[64];             /* uint64 for Bloom alignment */
	elf_lookup_object o;
};

static void synth_init(struct synth *s)
{
	memset(s, 0, sizeof *s);
	s->nsym = 1;    /* index 0 is the reserved undefined symbol */
	s->strn = 1;    /* offset 0 is the empty string */
}

static uint32_t synth_add(struct synth *s, const char *name, unsigned char bind,
                          unsigned char type, uint16_t shndx, uint64_t value)
{
	uint32_t i = s->nsym++;
	uint32_t off = s->strn;
	strcpy(&s->str[off], name);
	s->strn += (uint32_t) strlen(name) + 1;
	s->sym[i].st_name = off;
	s->sym[i].st_info = (unsigned char) ((bind << 4) | (type & 0xf));
	s->sym[i].st_shndx = shndx;
	s->sym[i].st_value = value;
	return i;
}

static void synth_finish(struct synth *s, const char *name)
{
	s->o.name = name;
	s->o.strtab = s->str;
	s->o.strsz = s->strn;
	s->o.symtab = s->sym;
	s->o.symcount = s->nsym;
}

/* A SysV .hash over the named symbols: head-insert each into its bucket. */
static void synth_build_sysv(struct synth *s, uint32_t nbucket)
{
	uint32_t *t = s->sysv, *bucket = &t[2], *chain, b, i;
	t[0] = nbucket;
	t[1] = s->nsym;
	chain = &bucket[nbucket];
	for (b = 0; b < nbucket; b++) bucket[b] = STN_UNDEF;
	for (i = 0; i < s->nsym; i++) chain[i] = STN_UNDEF;
	for (i = 1; i < s->nsym; i++) {
		uint32_t h;
		if (s->sym[i].st_name == 0) continue;
		h = elf_sysv_hash(&s->str[s->sym[i].st_name]) % nbucket;
		chain[i] = bucket[h];
		bucket[h] = i;
	}
	s->o.sysv_hash = s->sysv;
}

/* A minimal GNU .gnu.hash: one bucket holding every symbol from index 1, so the
 * chain is the symbols in order. Valid because a single bucket is trivially
 * sorted; enough to exercise the Bloom filter and the chain walk. */
static void synth_build_gnu(struct synth *s)
{
	uint32_t *g = (uint32_t *) s->gnu;
	uint32_t symoffset = 1, bloom_shift = 6, i;
	uint64_t *bloom;
	uint32_t *buckets, *chain;
	g[0] = 1; g[1] = symoffset; g[2] = 1; g[3] = bloom_shift;
	bloom = (uint64_t *) &g[4];
	bloom[0] = 0;
	buckets = (uint32_t *) &bloom[1];
	chain = &buckets[1];
	buckets[0] = symoffset;
	for (i = symoffset; i < s->nsym; i++) {
		uint32_t h = elf_gnu_hash(&s->str[s->sym[i].st_name]);
		bloom[0] |= (uint64_t) 1 << (h % 64);
		bloom[0] |= (uint64_t) 1 << ((h >> bloom_shift) % 64);
		chain[i - symoffset] = (h & ~1u) | (i == s->nsym - 1 ? 1u : 0u);
	}
	s->o.gnu_hash = (const uint32_t *) s->gnu;
}

/* A one-object probe, hashes computed here. */
static uint32_t probe(const elf_lookup_object *o, const char *name)
{
	unsigned char b; int d;
	return elf_object_find(o, name, elf_gnu_hash(name), elf_sysv_hash(name),
	                       NULL, &b, &d);
}

/* A version matcher that rejects any candidate in a named object. */
struct vmctx { const char *reject; };
static int reject_by_name(const elf_lookup_object *o, uint32_t idx, void *ctx)
{
	struct vmctx *v = ctx;
	(void) idx;
	return (v->reject && strcmp(o->name, v->reject) == 0) ? -1 : 1;
}

static void unit_hashfns(void)
{
	printf("  hash functions\n");
	ck("sysv_hash(\"\") == 0", elf_sysv_hash("") == 0);
	ck("gnu_hash(\"\") == 5381", elf_gnu_hash("") == 5381u);
	ck("sysv_hash is deterministic", elf_sysv_hash("printf") == elf_sysv_hash("printf"));
	ck("gnu_hash distinguishes names", elf_gnu_hash("printf") != elf_gnu_hash("scanf"));
}

static void unit_sysv(void)
{
	struct synth s;
	uint32_t a, b, c;
	printf("  SysV hash probe\n");
	synth_init(&s);
	a = synth_add(&s, "alpha", STB_GLOBAL, STT_FUNC, 1, 0x100);
	b = synth_add(&s, "beta",  STB_GLOBAL, STT_FUNC, 1, 0x200);
	c = synth_add(&s, "gamma", STB_GLOBAL, STT_OBJECT, 1, 0x300);
	synth_finish(&s, "sysv");
	synth_build_sysv(&s, 3);
	ck("finds alpha", probe(&s.o, "alpha") == a);
	ck("finds beta",  probe(&s.o, "beta")  == b);
	ck("finds gamma", probe(&s.o, "gamma") == c);
	ck("rejects absent name", probe(&s.o, "delta") == STN_UNDEF);
}

static void unit_gnu(void)
{
	struct synth s;
	uint32_t a, b;
	printf("  GNU hash probe\n");
	synth_init(&s);
	a = synth_add(&s, "one",   STB_GLOBAL, STT_FUNC, 1, 0x100);
	b = synth_add(&s, "two",   STB_GLOBAL, STT_FUNC, 1, 0x200);
	(void) synth_add(&s, "three", STB_GLOBAL, STT_FUNC, 1, 0x300);
	synth_finish(&s, "gnu");
	synth_build_gnu(&s);
	ck("finds one", probe(&s.o, "one") == a);
	ck("finds two", probe(&s.o, "two") == b);
	ck("rejects absent name (Bloom or chain)", probe(&s.o, "nope") == STN_UNDEF);
	ck("undefined ref is not a definition", probe(&s.o, "") == STN_UNDEF);
}

static void unit_binding(void)
{
	struct synth wobj, gobj, solo, p, q;
	elf_scope sc;
	elf_lookup_result r;
	printf("  binding rule and scope order\n");

	/* weak in the earlier object, global in the later: the global wins. */
	synth_init(&wobj); synth_add(&wobj, "dup", STB_WEAK, STT_FUNC, 1, 0xA);
	synth_finish(&wobj, "weaklib");
	synth_init(&gobj); synth_add(&gobj, "dup", STB_GLOBAL, STT_FUNC, 1, 0xB);
	synth_finish(&gobj, "globlib");
	elf_scope_init(&sc);
	elf_scope_add(&sc, &wobj.o);
	elf_scope_add(&sc, &gobj.o);
	elf_lookup(&sc, NULL, NULL, "dup", NULL, &r);
	ck("global overrides an earlier weak", r.found && r.value == 0xB &&
	   r.bind == STB_GLOBAL);

	/* only a weak exists: it is the answer. */
	synth_init(&solo); synth_add(&solo, "solo", STB_WEAK, STT_FUNC, 1, 0xC);
	synth_finish(&solo, "sololib");
	elf_scope_init(&sc);
	elf_scope_add(&sc, &solo.o);
	elf_lookup(&sc, NULL, NULL, "solo", NULL, &r);
	ck("lone weak resolves to the weak", r.found && r.value == 0xC &&
	   r.bind == STB_WEAK);

	/* interposition is scope order: two globals, the earlier one wins. */
	synth_init(&p); synth_add(&p, "f", STB_GLOBAL, STT_FUNC, 1, 0xF0);
	synth_finish(&p, "first");
	synth_init(&q); synth_add(&q, "f", STB_GLOBAL, STT_FUNC, 1, 0xF1);
	synth_finish(&q, "second");
	elf_scope_init(&sc);
	elf_scope_add(&sc, &p.o);
	elf_scope_add(&sc, &q.o);
	elf_lookup(&sc, NULL, NULL, "f", NULL, &r);
	ck("first definition in scope order wins", r.found && r.value == 0xF0);
	elf_scope_init(&sc);
	elf_scope_add(&sc, &q.o);
	elf_scope_add(&sc, &p.o);
	elf_lookup(&sc, NULL, NULL, "f", NULL, &r);
	ck("reversing the scope reverses the winner", r.found && r.value == 0xF1);

	/* an undefined name resolves to nothing. */
	elf_lookup(&sc, NULL, NULL, "absent", NULL, &r);
	ck("unresolved reference reports not found", !r.found);

	/* the local scope is searched after the global. */
	elf_scope_init(&sc);
	elf_scope_add(&sc, &wobj.o);          /* global: only a weak "dup" */
	{
		elf_scope loc;
		elf_scope_init(&loc);
		elf_scope_add(&loc, &gobj.o);     /* local: a global "dup" */
		elf_lookup(&sc, &loc, NULL, "dup", NULL, &r);
		ck("a global in the local scope beats a weak in the global",
		   r.found && r.value == 0xB);
	}
}

static void unit_version_seam(void)
{
	struct synth oldl, newl;
	struct vmctx vc;
	elf_version_matcher vm;
	elf_scope sc;
	elf_lookup_result r;
	printf("  version matcher seam\n");

	synth_init(&oldl); synth_add(&oldl, "v", STB_GLOBAL, STT_FUNC, 1, 0x01);
	synth_finish(&oldl, "old");
	synth_init(&newl); synth_add(&newl, "v", STB_GLOBAL, STT_FUNC, 1, 0x02);
	synth_finish(&newl, "new");
	elf_scope_init(&sc);
	elf_scope_add(&sc, &oldl.o);
	elf_scope_add(&sc, &newl.o);

	elf_lookup(&sc, NULL, NULL, "v", NULL, &r);
	ck("unversioned: the first definition wins", r.found && r.value == 0x01);

	vc.reject = "old";
	vm.match = reject_by_name;
	vm.ctx = &vc;
	elf_lookup(&sc, NULL, NULL, "v", &vm, &r);
	ck("a matcher rejecting the first falls through to the next",
	   r.found && r.value == 0x02);
}

static int run_unit(void)
{
	printf("== unit\n");
	unit_hashfns();
	unit_sysv();
	unit_gnu();
	unit_binding();
	unit_version_seam();
	printf("\n%s: %d failure(s)\n", failures ? "unit FAILED" : "unit ok", failures);
	return failures ? 1 : 0;
}

/* ---- collide mode: this loader's side of the differential --------------- */

struct placed {
	unsigned char *image;
	size_t         size;
	elf_parsed     parsed;
	elf_mapping    map;
};

static unsigned char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long n; unsigned char *buf;
	if (!f) { fprintf(stderr, "lookup_test: cannot open %s\n", path); return NULL; }
	if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0) { fclose(f); return NULL; }
	rewind(f);
	buf = malloc((size_t) n);
	if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) {
		free(buf); fclose(f); return NULL;
	}
	fclose(f);
	*size = (size_t) n;
	return buf;
}

static int place(const char *path, uint64_t base, struct placed *o)
{
	elf_diag pd; elf_map_diag md;
	if (!(o->image = slurp(path, &o->size))) return -1;
	if (elf_parse(o->image, o->size, &o->parsed, &pd) != elf_ok) {
		fprintf(stderr, "lookup_test: %s: parse: %s\n", path, pd.msg);
		return -1;
	}
	if (elf_map(o->image, o->size, &o->parsed, base, &o->map, &md) != elf_map_ok) {
		fprintf(stderr, "lookup_test: %s: map: %s\n", path, md.msg);
		return -1;
	}
	return 0;
}

/* Discover an object's dynamic view with WP-34, then present it as a lookup
 * object. The reloc scope is only a vehicle for the discovery elf_reloc_add
 * already does; the lookup engine reads the fields it fills. */
static int as_lookup(elf_reloc_scope *rs, struct placed *p, const char *name,
                     elf_lookup_object *lk)
{
	elf_reloc_diag d;
	unsigned i = rs->count;
	if (elf_reloc_add(rs, &p->map, &p->parsed, name, &d) != elf_reloc_ok) {
		fprintf(stderr, "lookup_test: %s: discover: %s\n", name, d.msg);
		return -1;
	}
	memset(lk, 0, sizeof *lk);
	lk->name = rs->obj[i].name;
	lk->bias = rs->obj[i].bias;
	lk->strtab = rs->obj[i].strtab;
	lk->strsz = rs->obj[i].strsz;
	lk->symtab = rs->obj[i].symtab;
	lk->symcount = rs->obj[i].symcount;
	lk->sysv_hash = rs->obj[i].sysv_hash;
	lk->gnu_hash = rs->obj[i].gnu_hash;
	return 0;
}

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static int run_collide(int argc, char **argv)
{
	/* args: GRAPHDIR MAIN [--preload PATH]... [--expect TAG] */
	static struct placed pl[ELF_RELOC_MAX_OBJ];
	static elf_lookup_object lk[ELF_RELOC_MAX_OBJ];
	static elf_reloc_scope rs;
	elf_lookup_object main_lk;
	const elf_lookup_object *deps[ELF_RELOC_MAX_OBJ];
	const elf_lookup_object *pre[ELF_RELOC_MAX_OBJ];
	unsigned ndep = 0, npre = 0, used = 0, i;
	const char *graphdir, *mainpath;
	const char *preloads[ELF_RELOC_MAX_OBJ];
	int expect = -1, npreload = 0;
	uint64_t base = 0x50000000ULL;
	elf_graph_config cfg;
	elf_graph g;
	const char *dp[1];
	elf_scope global;
	elf_lookup_result r;
	int tag, rc = 0;

	if (argc < 2) { fprintf(stderr, "collide: need GRAPHDIR MAIN\n"); return 2; }
	graphdir = argv[0];
	mainpath = argv[1];
	for (i = 2; i < (unsigned) argc; i++) {
		if (!strcmp(argv[i], "--preload") && i + 1 < (unsigned) argc)
			preloads[npreload++] = argv[++i];
		else if (!strcmp(argv[i], "--expect") && i + 1 < (unsigned) argc)
			expect = atoi(argv[++i]);
	}

	elf_reloc_scope_init(&rs);
	memset(&main_lk, 0, sizeof main_lk);
	main_lk.name = base_name(mainpath);   /* the root defines no collide */

	/* Walk the graph with WP-33 to get the dependency load order. */
	memset(&cfg, 0, sizeof cfg);
	dp[0] = graphdir;
	cfg.default_paths = dp;
	cfg.default_count = 1;
	if (elf_graph_build(mainpath, &cfg, &g) != 0 || g.error) {
		fprintf(stderr, "collide: graph: %s\n", g.errmsg);
		elf_graph_free(&g);
		return 1;
	}

	/* Map and present every found dependency, in load order (node 0 is the
	 * root, already represented by main_lk). */
	for (i = 1; i < g.count && used < ELF_RELOC_MAX_OBJ; i++) {
		if (!g.obj[i].found)
			continue;
		if (place(g.obj[i].path, base, &pl[used]) != 0) { rc = 1; goto done; }
		if (as_lookup(&rs, &pl[used], g.obj[i].soname, &lk[used]) != 0) {
			rc = 1; goto done;
		}
		deps[ndep++] = &lk[used];
		used++;
		base += 0x2000000ULL;
	}

	/* Map and present the preload objects (LD_PRELOAD interposers). */
	for (i = 0; i < (unsigned) npreload && used < ELF_RELOC_MAX_OBJ; i++) {
		if (place(preloads[i], base, &pl[used]) != 0) { rc = 1; goto done; }
		if (as_lookup(&rs, &pl[used], base_name(preloads[i]), &lk[used]) != 0) {
			rc = 1; goto done;
		}
		pre[npre++] = &lk[used];
		used++;
		base += 0x2000000ULL;
	}

	/* Global scope in canonical order: root, preloads, then the closure. */
	if (elf_scope_build_global(&global, &main_lk, pre, npre, deps, ndep) != 0) {
		fprintf(stderr, "collide: scope overflow\n"); rc = 1; goto done;
	}

	if (elf_lookup(&global, NULL, NULL, "collide", NULL, &r) != 0 || !r.found) {
		printf("winner=none tag=-1\n");
		rc = 1; goto done;
	}

	/* collide() is a constant-returning leaf, so calling the resolved pointer
	 * reads the winning tag directly -- the same value a real ld.so's process
	 * exits with. */
	tag = ((int (*)(void)) (uintptr_t) r.value)();
	printf("winner=%s tag=%d\n", r.obj->name, tag);
	if (expect >= 0 && tag != expect) {
		fprintf(stderr, "collide: expected tag %d, got %d\n", expect, tag);
		rc = 1;
	}

done:
	elf_graph_free(&g);
	return rc;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "unit"))
		return run_unit();
	if (argc >= 2 && !strcmp(argv[1], "collide"))
		return run_collide(argc - 2, argv + 2);
	fprintf(stderr, "usage: %s unit | collide GRAPHDIR MAIN"
	        " [--preload PATH]... [--expect TAG]\n", argv[0]);
	return 2;
}
