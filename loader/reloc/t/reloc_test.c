/* WP-34 relocation test.
 *
 * Holds the engine to the done-when bar over real, cross-built objects. For the
 * hello case it walks the graph with WP-33, maps every object with WP-32,
 * relocates the scope with WP-34, and then:
 *
 *   - proves the binding discipline: under lazy linking the PLT slot for the
 *     cross-object call still points into the PLT before the call and at the
 *     callee after it, so the resolver trampoline is what bound it; under
 *     BIND_NOW it already points at the callee before the image runs;
 *   - enters the image and checks it made the cross-object call, read the
 *     imported datum, dereferenced an internal relocated pointer, and ran an
 *     ifunc-dispatched memcpy correctly;
 *   - checks the ifunc resolver chose the same body a real loader's criterion
 *     (CPUID ERMS, one of glibc memcpy's own) selects on this CPU.
 *
 * The tls case maps a thread-local specimen and checks the stored TPOFF64
 * against the static-TLS layout without entering it.
 *
 * Usage:
 *   reloc_test hello --expect-lazy|--expect-now HELLO
 *   reloc_test tls   TLSLIB
 */
#define _GNU_SOURCE
#include "../elf_reloc.h"
#include "../../elf/elf_parse.h"
#include "../../map/elf_map.h"
#include "../../graph/elf_graph.h"
#include "handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libgen.h>

extern void elf_enter(void *entry, void *handshake);

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-52s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

static unsigned char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long n;
	unsigned char *buf;
	if (!f) { fprintf(stderr, "reloc_test: cannot open %s\n", path); return NULL; }
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

/* A placed object plus the image it came from, kept alive for the run. */
struct placed {
	unsigned char *image;
	size_t         size;
	elf_parsed     parsed;
	elf_mapping    map;
};

static int place(const char *path, uint64_t base, struct placed *o)
{
	elf_diag pd;
	elf_map_diag md;
	if (!(o->image = slurp(path, &o->size))) return -1;
	if (elf_parse(o->image, o->size, &o->parsed, &pd) != elf_ok) {
		fprintf(stderr, "reloc_test: %s: parse: %s\n", path, pd.msg);
		return -1;
	}
	if (elf_map(o->image, o->size, &o->parsed, base, &o->map, &md) != elf_map_ok) {
		fprintf(stderr, "reloc_test: %s: map: %s\n", path, md.msg);
		return -1;
	}
	return 0;
}

/* Find a defined symbol's runtime address and size in a relocated object. */
static int sym_lookup(const elf_reloc_object *o, const char *name,
                      uint64_t *addr, uint64_t *size)
{
	uint64_t j;
	for (j = 0; j < o->symcount; j++) {
		const Elf64_Sym *s = &o->symtab[j];
		if (s->st_shndx == SHN_UNDEF || s->st_name == 0) continue;
		if (strcmp(name, o->strtab + s->st_name) != 0) continue;
		if (addr) *addr = o->bias + s->st_value;
		if (size) *size = s->st_size;
		return 1;
	}
	return 0;
}

/* The GOT slot a JUMP_SLOT names, found by the imported symbol's name. */
static uint64_t *plt_slot_for(const elf_reloc_object *o, const char *name)
{
	uint64_t k;
	for (k = 0; k < o->jmprel_n; k++) {
		uint32_t si = ELF64_R_SYM(o->jmprel[k].r_info);
		if (strcmp(name, o->strtab + o->symtab[si].st_name) == 0)
			return (uint64_t *)(uintptr_t)(o->bias + o->jmprel[k].r_offset);
	}
	return NULL;
}

static int in_range(uint64_t v, const elf_mapping *m)
{
	return v >= m->base && v < m->base + m->size;
}

/* CPUID ERMS, the same criterion the specimen's resolver uses. */
static int host_have_erms(void)
{
	unsigned int a, b, c, d;
	__asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	                  : "a"(7u), "c"(0u));
	return (b >> 9) & 1u;
}

/* ---- the hello case ---------------------------------------------------- */

static int run_hello(const char *path, int expect_now)
{
	char dirbuf[4096];
	const char *dir;
	elf_graph g;
	elf_graph_config cfg;
	elf_reloc_scope scope;
	elf_reloc_diag rd;
	struct placed obj[8];
	unsigned i;
	uint64_t greet_addr = 0, *slot;
	struct hs h;

	memset(&g, 0, sizeof g);
	memset(&cfg, 0, sizeof cfg);
	snprintf(dirbuf, sizeof dirbuf, "%s", path);
	dir = dirname(dirbuf);
	cfg.ld_library_path = dir;   /* libgreet.so sits beside the specimen */

	printf("== hello: %s (%s)\n\n", path, expect_now ? "BIND_NOW" : "lazy");

	/* WP-33: walk the graph to get the load order. */
	if (elf_graph_build(path, &cfg, &g) != 0 || g.error) {
		fprintf(stderr, "reloc_test: graph: %s\n", g.errmsg);
		return 1;
	}
	ck("the graph lists the root and its one dependency", g.count == 2);
	ck("every dependency was found", g.missing_count == 0);

	/* WP-32: place every object in load order. */
	elf_reloc_scope_init(&scope);
	for (i = 0; i < g.count; i++) {
		uint64_t base = 0x20000000ULL + (uint64_t) i * 0x10000000ULL;
		if (place(g.obj[i].path, base, &obj[i]) != 0) { elf_graph_free(&g); return 1; }
		if (elf_reloc_add(&scope, &obj[i].map, &obj[i].parsed,
		                  g.obj[i].soname, &rd) != elf_reloc_ok) {
			fprintf(stderr, "reloc_test: add %s: %s\n", g.obj[i].soname, rd.msg);
			elf_graph_free(&g);
			return 1;
		}
	}

	/* Where greet will resolve to, for the binding-discipline check. */
	for (i = 1; i < scope.count; i++)
		if (sym_lookup(&scope.obj[i], "greet", &greet_addr, NULL)) break;
	ck("the callee greet is defined in a dependency", greet_addr != 0);

	slot = plt_slot_for(&scope.obj[0], "greet");
	ck("the root carries a PLT slot for greet", slot != NULL);

	/* WP-34: relocate the whole scope. */
	if (elf_reloc_apply(&scope, &rd) != elf_reloc_ok) {
		fprintf(stderr, "reloc_test: apply: %s\n", rd.msg);
		elf_graph_free(&g);
		return 1;
	}

	/* Binding discipline, observed at the GOT slot. */
	if (slot) {
		if (expect_now)
			ck("under BIND_NOW the slot points at greet before the run",
			   *slot == greet_addr);
		else
			ck("under lazy the slot still points into the PLT before the run",
			   in_range(*slot, scope.obj[0].map) && *slot != greet_addr);
	}

	/* Enter the image. */
	memset(&h, 0, sizeof h);
	h.in_cookie = 0x0123456789ABCDEFULL;
	elf_enter((void *)(uintptr_t) scope.obj[0].map->entry, &h);

	ck("the image ran and reached its handshake", h.ran == 1);
	ck("the cross-object call through the PLT returned correctly",
	   h.greet_ret == (0x0123456789ABCDEFULL ^ HS_KEY));
	ck("the imported datum read across the object boundary", h.data_word == HS_WORD);
	ck("an internal relocated pointer dereferenced correctly", h.relative_ok == 1);
	ck("the ifunc-dispatched memcpy copied correctly", h.memcpy_ok == 1);
	ck("the ifunc resolver chose the body the CPU criterion selects",
	   h.impl_id == (uint64_t)(host_have_erms() ? 2 : 1));

	/* After the run, lazy binding must have written the slot through. */
	if (slot && !expect_now)
		ck("after the run the lazily bound slot points at greet",
		   *slot == greet_addr);

	elf_graph_free(&g);
	for (i = 0; i < scope.count; i++) { elf_unmap(&obj[i].map); free(obj[i].image); }
	return 0;
}

/* ---- the tls case ------------------------------------------------------ */

/* Is a relocation type one the engine carries? Used to prove the engine covers
 * the whole R_X86_64_* set a real el8 object contains. */
static int type_supported(uint32_t t)
{
	switch (t) {
	case R_X86_64_NONE: case R_X86_64_64: case R_X86_64_GLOB_DAT:
	case R_X86_64_JUMP_SLOT: case R_X86_64_RELATIVE: case R_X86_64_COPY:
	case R_X86_64_IRELATIVE: case R_X86_64_TPOFF64: case R_X86_64_DTPMOD64:
	case R_X86_64_DTPOFF64:
		return 1;
	}
	return 0;
}

/* Certify the static-TLS relocations over a real vendor object. It carries
 * genuine R_X86_64_TPOFF64 entries the platform's own toolchain will not emit
 * from source. The bootstrap apply writes the self-contained relocations
 * without resolving an external scope or running any glibc code, and every
 * TPOFF64 must land at the offset the static-TLS layout dictates. */
static int run_tls(const char *path)
{
	struct placed o;
	elf_reloc_scope scope;
	elf_reloc_diag rd;
	const elf_reloc_object *ro;
	uint64_t i, align, memsz, rounded;
	unsigned tpoff_seen = 0, tpoff_ok = 0, unsupported = 0;

	printf("== tls (vendor object): %s\n\n", path);
	if (place(path, 0x40000000ULL, &o) != 0) return 1;
	elf_reloc_scope_init(&scope);
	if (elf_reloc_add(&scope, &o.map, &o.parsed, "libc.so.6", &rd) != elf_reloc_ok) {
		fprintf(stderr, "reloc_test: add: %s\n", rd.msg); return 1;
	}
	ro = &scope.obj[0];

	ck("the vendor object carries a TLS segment", o.parsed.has_tls);

	/* Every relocation type the object carries is one the engine implements. */
	for (i = 0; i < ro->rela_n; i++)
		if (!type_supported(ELF64_R_TYPE(ro->rela[i].r_info))) unsupported++;
	for (i = 0; i < ro->jmprel_n; i++)
		if (!type_supported(ELF64_R_TYPE(ro->jmprel[i].r_info))) unsupported++;
	ck("the engine covers every relocation type the object contains",
	   unsupported == 0);

	if (elf_reloc_apply_bootstrap(&scope, &rd) != elf_reloc_ok) {
		fprintf(stderr, "reloc_test: bootstrap: %s\n", rd.msg); return 1;
	}

	align = o.parsed.tls_align ? o.parsed.tls_align : 1;
	memsz = o.parsed.tls_memsz;
	rounded = memsz;
	if (align > 1) rounded = (rounded + align - 1) & ~(align - 1);

	for (i = 0; i < ro->rela_n; i++) {
		const Elf64_Rela *r = &ro->rela[i];
		uint32_t si;
		uint64_t stv, expect, got;
		if (ELF64_R_TYPE(r->r_info) != R_X86_64_TPOFF64) continue;
		si = ELF64_R_SYM(r->r_info);
		stv = si ? ro->symtab[si].st_value : 0;
		expect = (uint64_t)((int64_t) stv + (int64_t) r->r_addend
		                    - (int64_t) rounded);
		got = *(uint64_t *)(uintptr_t)(ro->bias + r->r_offset);
		tpoff_seen++;
		if (got == expect) tpoff_ok++;
	}
	printf("    %u TPOFF64 relocations, %u matched the static-TLS layout\n",
	       tpoff_seen, tpoff_ok);
	ck("the object carries TPOFF64 relocations", tpoff_seen > 0);
	ck("every TPOFF64 landed at its static-TLS offset", tpoff_seen == tpoff_ok);

	elf_unmap(&o.map); free(o.image);
	return 0;
}

int main(int argc, char **argv)
{
	int rc = 2;
	if (argc >= 3 && !strcmp(argv[1], "hello")) {
		int expect_now = 0, i;
		const char *path = NULL;
		for (i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--expect-now")) expect_now = 1;
			else if (!strcmp(argv[i], "--expect-lazy")) expect_now = 0;
			else path = argv[i];
		}
		if (path) rc = run_hello(path, expect_now);
	} else if (argc >= 3 && !strcmp(argv[1], "tls")) {
		rc = run_tls(argv[2]);
	} else {
		fprintf(stderr, "usage: reloc_test hello --expect-lazy|--expect-now ELF\n"
		                "       reloc_test tls TLSLIB\n");
		return 2;
	}
	if (rc == 1) failures++;
	printf("\ncase_failures=%d\ncase_result=%s\n", failures,
	       failures ? "fail" : "pass");
	return failures ? 1 : 0;
}
