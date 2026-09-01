/* WP-56 dynamic-crossing-driver test.
 *
 * Holds dyn_exec_link() to its contract over real, cross-built objects. The
 * pair is the reloc harness's own specimens reused: a "hello" main that imports
 * a function and a datum from a libgreet "runtime", except the main is linked
 * with a PT_INTERP so exec_kind_of() reads it as dynamic -- bzip2's shape at
 * the size the harness can enter without a libc. The test:
 *
 *   - proves the driver's guard: a null request, an image the classifier does
 *     not call dynamic, and a dynamic image with no runtime are each refused
 *     with the matching code and nothing is entered;
 *   - links the real pair through dyn_exec_link, and checks the scope it hands
 *     back has the main as its root and the runtime behind it, in that order;
 *   - checks the binding discipline at the GOT slot the way WP-34's own test
 *     does -- lazy leaves it in the PLT until the call binds it;
 *   - enters the linked image and checks it made the cross-object call, read
 *     the imported datum, followed an internal relocated pointer, and ran its
 *     ifunc memcpy: the crossing the driver composed actually works.
 *
 * Usage:  dyn_exec_test MAIN RUNTIME
 *   MAIN     the interp-bearing hello, linked lazy against RUNTIME
 *   RUNTIME  libgreet.so
 */
#define _GNU_SOURCE
#include "../dyn_exec.h"
#include "../exec_kind.h"
#include "../../elf/elf_parse.h"
#include "../../map/elf_map.h"
#include "../../reloc/elf_reloc.h"
#include "../../reloc/t/handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void elf_enter(void *entry, void *handshake);

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-56s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

static unsigned char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long n;
	unsigned char *buf;
	if (!f) { fprintf(stderr, "dyn_exec_test: cannot open %s\n", path); return NULL; }
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
		fprintf(stderr, "dyn_exec_test: %s: parse: %s\n", path, pd.msg);
		return -1;
	}
	if (elf_map(o->image, o->size, &o->parsed, base, &o->map, &md) != elf_map_ok) {
		fprintf(stderr, "dyn_exec_test: %s: map: %s\n", path, md.msg);
		return -1;
	}
	return 0;
}

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

int main(int argc, char **argv)
{
	struct placed mn, rt;
	dyn_exec_req req;
	dyn_exec_diag diag;
	const elf_reloc_scope *scope = NULL;
	dyn_exec_err e;
	uint64_t *slot;
	struct hs h;

	if (argc != 3) {
		fprintf(stderr, "usage: dyn_exec_test MAIN RUNTIME\n");
		return 2;
	}

	printf("== dyn_exec: %s against %s\n\n", argv[1], argv[2]);

	if (place(argv[1], 0x20000000ULL, &mn) != 0) return 1;
	if (place(argv[2], 0x40000000ULL, &rt) != 0) return 1;

	ck("the main image is the dynamic shape (carries PT_INTERP)",
	   exec_kind_of(&mn.parsed) == EXEC_KIND_DYNAMIC);
	ck("the runtime is not itself a program (no PT_INTERP)",
	   exec_kind_of(&rt.parsed) != EXEC_KIND_DYNAMIC);

	/* Guard: a null request is refused, nothing composed. */
	e = dyn_exec_link(NULL, &scope, &diag);
	ck("a null request is refused with arg", e == dyn_exec_err_arg);

	/* Guard: an image the classifier does not call dynamic is refused. The
	 * runtime, with no PT_INTERP, stands in for it. */
	req.main_map = &rt.map; req.main_p = &rt.parsed;
	req.rt_map = &rt.map;   req.rt_p = &rt.parsed;   req.rt_name = NULL;
	e = dyn_exec_link(&req, &scope, &diag);
	ck("a non-dynamic main is refused with not-dynamic",
	   e == dyn_exec_err_not_dynamic);

	/* Guard: a dynamic main with no runtime is refused. */
	req.main_map = &mn.map; req.main_p = &mn.parsed;
	req.rt_map = NULL;      req.rt_p = NULL;         req.rt_name = NULL;
	e = dyn_exec_link(&req, &scope, &diag);
	ck("a dynamic main with no runtime is refused with no-runtime",
	   e == dyn_exec_err_no_runtime);

	/* The real link. */
	req.main_map = &mn.map; req.main_p = &mn.parsed;
	req.rt_map = &rt.map;   req.rt_p = &rt.parsed;   req.rt_name = "libc.so.6";
	scope = NULL;
	e = dyn_exec_link(&req, &scope, &diag);
	if (e != dyn_exec_ok) {
		fprintf(stderr, "dyn_exec_test: link: %s at %s: %s (%s)\n",
			dyn_exec_err_name(e), diag.stage ? diag.stage : "?",
			diag.msg, diag.reloc.msg);
		return 1;
	}
	ck("dyn_exec_link reports ok and hands back a scope", scope != NULL);
	if (!scope) return 1;
	ck("the scope holds the pair", scope->count == 2);
	ck("the main image is the load-order root", scope->obj[0].map == &mn.map);
	ck("the runtime is the single dependency behind it",
	   scope->obj[1].map == &rt.map);

	/* Binding discipline: the main was linked lazy, so its slot for the
	 * cross-object call points into its own PLT until the call binds it. */
	slot = plt_slot_for(&scope->obj[0], "greet");
	ck("the root carries a PLT slot for the imported call", slot != NULL);
	if (slot)
		ck("under lazy linking the slot still points into the image",
		   in_range(*slot, scope->obj[0].map));

	/* Enter the composed image and read the handshake. */
	memset(&h, 0, sizeof h);
	h.in_cookie = 0x0123456789ABCDEFULL;
	elf_enter((void *)(uintptr_t) scope->obj[0].map->entry, &h);

	ck("the image ran and reached its handshake", h.ran == 1);
	ck("the cross-object call the driver linked returned correctly",
	   h.greet_ret == (0x0123456789ABCDEFULL ^ HS_KEY));
	ck("the imported datum read across the object boundary",
	   h.data_word == HS_WORD);
	ck("an internal relocated pointer dereferenced correctly",
	   h.relative_ok == 1);
	ck("the ifunc-dispatched memcpy copied correctly", h.memcpy_ok == 1);

	printf("\n%s\n", failures ? "a check failed" : "all checks passed");
	return failures ? 1 : 0;
}
