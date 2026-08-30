/* WP-38 unit certification: the order, the error protocol, and the two
 * questions a program asks about addresses.
 *
 * These cases build the object table directly rather than loading files, so
 * the order and the answers are checked against tables the case wrote and can
 * therefore predict exactly. Loading real objects, ten thousand times, is
 * dl_e2e.c's job; this is the part that must be right before that one means
 * anything.
 */
#include <stdio.h>
#include <string.h>

#include "../dl.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		printf("  FAIL %s\n", what);
	}
}

static void eq_str(const char *got, const char *want, const char *what)
{
	checks++;
	if (!got || strcmp(got, want) != 0) {
		failures++;
		printf("  FAIL %s: got \"%s\", want \"%s\"\n",
		       what, got ? got : "(null)", want);
	}
}

/* ---- the call log ------------------------------------------------------ */

static char log_buf[256];

static void logc(char c)
{
	size_t n = strlen(log_buf);
	if (n + 1 < sizeof log_buf) {
		log_buf[n] = c;
		log_buf[n + 1] = '\0';
	}
}

/* The initializers carry the same ABI attribute a real object's do, so the
 * synthetic case exercises the pointer type the loader actually calls through
 * rather than a host-ABI stand-in that would hide a wrong-register call. */
#define INITFN(letter, name) \
	static void ELFSYSV_SYSV_ABI name(int argc, char **argv, char **envp) \
	{ (void) argc; (void) argv; (void) envp; logc(letter); }

INITFN('A', init_a) INITFN('B', init_b) INITFN('C', init_c) INITFN('D', init_d)
INITFN('a', fini_a) INITFN('b', fini_b) INITFN('c', fini_c) INITFN('d', fini_d)
INITFN('P', preinit)  INITFN('1', init_arr1) INITFN('2', init_arr2)

/* ---- synthetic objects ------------------------------------------------- */

/* Each object gets its own dynamic array and its own array sections. The
 * arrays are plain C objects at absolute addresses, so a zero load bias makes
 * the loader's "bias + d_val" arithmetic the identity. */
struct synth {
	Elf64_Dyn dyn[10];
	uint64_t  init_arr[2];
	uint64_t  fini_arr[2];
	uint64_t  pre_arr[1];
};

static struct synth synth[4];

static dl_object *mk(dl_state *st, unsigned slot, const char *name,
                     dl_init_fn init, dl_init_fn fini)
{
	dl_object *o = &st->obj[slot];
	struct synth *s = &synth[slot];
	unsigned d = 0;

	memset(o, 0, sizeof *o);
	memset(s, 0, sizeof *s);

	o->in_use = 1;
	o->slot = slot;
	o->seq = slot;
	snprintf(o->path, sizeof o->path, "/lib/%s", name);
	snprintf(o->soname, sizeof o->soname, "%s", name);
	o->map.load_bias = 0;

	if (init) {
		s->dyn[d].d_tag = DT_INIT;
		s->dyn[d].d_un.d_ptr = (uint64_t)(uintptr_t) init;
		d++;
	}
	if (fini) {
		s->dyn[d].d_tag = DT_FINI;
		s->dyn[d].d_un.d_ptr = (uint64_t)(uintptr_t) fini;
		d++;
	}
	s->dyn[d].d_tag = DT_NULL;

	o->dyn = s->dyn;
	if (slot + 1 > st->obj_count)
		st->obj_count = slot + 1;
	return o;
}

/* Append tag/value to an object's dynamic array, before its DT_NULL. */
static void add_dyn(unsigned slot, int64_t tag, uint64_t val)
{
	struct synth *s = &synth[slot];
	unsigned d = 0;
	while (s->dyn[d].d_tag != DT_NULL)
		d++;
	s->dyn[d].d_tag = tag;
	s->dyn[d].d_un.d_val = val;
	s->dyn[d + 1].d_tag = DT_NULL;
}

/* ---- the cases --------------------------------------------------------- */

/* A chain: A needs B needs C. Dependencies first means C, B, A, and teardown
 * is the exact reverse. */
static void case_chain(void)
{
	static dl_state st;
	unsigned want[3], order[8], n;
	unsigned i;

	printf("case: a dependency chain initializes leaf first\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	mk(&st, 0, "a.so", init_a, fini_a);
	mk(&st, 1, "b.so", init_b, fini_b);
	mk(&st, 2, "c.so", init_c, fini_c);
	dl_add_dep(&st.obj[0], 1);
	dl_add_dep(&st.obj[1], 2);

	want[0] = 0; want[1] = 1; want[2] = 2;
	n = dl_init_order(&st, want, 3, order, 8);
	ok(n == 3, "the order covers every object");
	ok(order[0] == 2 && order[1] == 1 && order[2] == 0,
	   "the order is c, b, a");

	log_buf[0] = '\0';
	/* Slot 0 is not a startup root here, so no DT_PREINIT_ARRAY runs. */
	dl_run_init(&st, order, n);
	eq_str(log_buf, "CBA", "initializers ran leaf first");

	log_buf[0] = '\0';
	for (i = n; i-- > 0; )
		dl_run_fini(&st, &st.obj[order[i]]);
	eq_str(log_buf, "abc", "finalizers ran in the exact reverse");

	ok(dl_run_init(&st, order, n) == 3,
	   "an object finalized may be initialized again");
}

/* A diamond: A needs B and C, both need D. D must precede B and C, and both
 * must precede A; D must run exactly once. */
static void case_diamond(void)
{
	static dl_state st;
	unsigned want[4], order[8], n;

	printf("case: a diamond runs the shared dependency once\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	mk(&st, 0, "a.so", init_a, fini_a);
	mk(&st, 1, "b.so", init_b, fini_b);
	mk(&st, 2, "c.so", init_c, fini_c);
	mk(&st, 3, "d.so", init_d, fini_d);
	dl_add_dep(&st.obj[0], 1);
	dl_add_dep(&st.obj[0], 2);
	dl_add_dep(&st.obj[1], 3);
	dl_add_dep(&st.obj[2], 3);

	want[0] = 0; want[1] = 1; want[2] = 2; want[3] = 3;
	n = dl_init_order(&st, want, 4, order, 8);
	ok(n == 4, "every object is in the order once");

	log_buf[0] = '\0';
	dl_run_init(&st, order, n);
	eq_str(log_buf, "DBCA", "the shared dependency ran first and once");
}

/* A cycle: A needs B, B needs A. "Dependencies first" cannot hold for both, so
 * the tie-break is the specification: the walk is seeded in load order, and the
 * edge that re-enters an object already on the walk is the one dropped. Both
 * objects initialize, exactly once each, in an order that does not change. */
static void case_cycle(void)
{
	static dl_state st;
	unsigned want[2], order[8], n;
	unsigned i;

	printf("case: a dependency cycle has a written-down tie-break\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	mk(&st, 0, "a.so", init_a, fini_a);
	mk(&st, 1, "b.so", init_b, fini_b);
	dl_add_dep(&st.obj[0], 1);
	dl_add_dep(&st.obj[1], 0);

	want[0] = 0; want[1] = 1;
	n = dl_init_order(&st, want, 2, order, 8);
	ok(n == 2, "both objects are in the order");
	ok(order[0] == 1 && order[1] == 0,
	   "the closing edge is dropped, not the whole cycle");

	log_buf[0] = '\0';
	dl_run_init(&st, order, n);
	eq_str(log_buf, "BA", "each cycle member initialized exactly once");

	/* Determinism: the same graph gives the same order every time. */
	for (i = 0; i < 8; i++) {
		unsigned again[8];
		unsigned m = dl_init_order(&st, want, 2, again, 8);
		ok(m == n && again[0] == order[0] && again[1] == order[1],
		   "the tie-break is the same on every walk");
	}
}

/* DT_PREINIT_ARRAY belongs to the program alone and runs before any DT_INIT
 * anywhere; DT_INIT precedes DT_INIT_ARRAY; DT_FINI_ARRAY runs backwards. */
static void case_arrays(void)
{
	static dl_state st;
	unsigned want[1], order[4], n;

	printf("case: preinit, init and the arrays run in the ABI's order\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	mk(&st, 0, "prog", init_a, fini_a);
	st.obj[0].is_startup = 1;

	synth[0].pre_arr[0] = (uint64_t)(uintptr_t) preinit;
	synth[0].init_arr[0] = (uint64_t)(uintptr_t) init_arr1;
	synth[0].init_arr[1] = (uint64_t)(uintptr_t) init_arr2;
	synth[0].fini_arr[0] = (uint64_t)(uintptr_t) init_arr1;
	synth[0].fini_arr[1] = (uint64_t)(uintptr_t) init_arr2;

	add_dyn(0, DT_PREINIT_ARRAY, (uint64_t)(uintptr_t) synth[0].pre_arr);
	add_dyn(0, DT_PREINIT_ARRAYSZ, sizeof synth[0].pre_arr);
	add_dyn(0, DT_INIT_ARRAY, (uint64_t)(uintptr_t) synth[0].init_arr);
	add_dyn(0, DT_INIT_ARRAYSZ, sizeof synth[0].init_arr);
	add_dyn(0, DT_FINI_ARRAY, (uint64_t)(uintptr_t) synth[0].fini_arr);
	add_dyn(0, DT_FINI_ARRAYSZ, sizeof synth[0].fini_arr);

	want[0] = 0;
	n = dl_init_order(&st, want, 1, order, 4);
	log_buf[0] = '\0';
	dl_run_init(&st, order, n);
	eq_str(log_buf, "PA12", "preinit, then DT_INIT, then the array in order");

	log_buf[0] = '\0';
	dl_run_fini(&st, &st.obj[0]);
	eq_str(log_buf, "21a", "the fini array runs backwards, then DT_FINI");
}

/* dlerror reports once and then reports nothing, which is the contract every
 * caller's error handling is written against. */
static void case_error_protocol(void)
{
	static dl_state st;
	const char *e;

	printf("case: dlerror reports an error once\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);

	ok(dl_error(&st) == NULL, "no error is pending on a fresh state");
	ok(dl_open(&st, "", RTLD_NOW) == NULL, "an empty filename fails");
	e = dl_error(&st);
	ok(e != NULL && strstr(e, "dlopen") != NULL, "the failure is reported");
	ok(dl_error(&st) == NULL, "the second read reports nothing");

	ok(dl_sym(&st, RTLD_DEFAULT, "nothing_defines_this") == NULL,
	   "an undefined symbol resolves to nothing");
	e = dl_error(&st);
	ok(e != NULL && strstr(e, "undefined symbol") != NULL,
	   "and says so");
}

/* ---- dladdr and dl_iterate_phdr over a synthetic image ----------------- */

static char fake_image[0x2000];
static Elf64_Sym fake_syms[4];
static char fake_strs[64];
static Elf64_Phdr fake_phdr[3];

static void build_fake(dl_state *st)
{
	dl_object *o = mk(st, 0, "plug.so", NULL, NULL);
	uint64_t base = (uint64_t)(uintptr_t) fake_image;

	memcpy(fake_strs, "\0alpha\0beta", 12);
	fake_syms[1].st_name = 1;             /* "alpha" at +0x100, size 0x80 */
	fake_syms[1].st_value = 0x100;
	fake_syms[1].st_size = 0x80;
	fake_syms[1].st_shndx = 1;
	fake_syms[1].st_info = (STB_GLOBAL << 4) | STT_FUNC;
	fake_syms[2].st_name = 7;             /* "beta" at +0x400, size 0x10 */
	fake_syms[2].st_value = 0x400;
	fake_syms[2].st_size = 0x10;
	fake_syms[2].st_shndx = 1;
	fake_syms[2].st_info = (STB_GLOBAL << 4) | STT_FUNC;

	o->map.base = base;
	o->map.size = sizeof fake_image;
	o->map.load_bias = base;
	o->lo.name = o->soname;
	o->lo.bias = base;
	o->lo.strtab = fake_strs;
	o->lo.strsz = sizeof fake_strs;
	o->lo.symtab = fake_syms;
	o->lo.symcount = 3;

	fake_phdr[0].p_type = PT_LOAD;
	fake_phdr[1].p_type = PT_GNU_EH_FRAME;
	fake_phdr[1].p_vaddr = 0x800;
	fake_phdr[2].p_type = PT_TLS;
	o->phdr = fake_phdr;
	o->phnum = 3;
}

struct walk_seen {
	unsigned n;
	uint64_t eh_frame;
	const char *name;
};

static int walk_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	struct walk_seen *w = data;
	unsigned i;

	if (size < sizeof *info)
		return -1;
	w->n++;
	w->name = info->dlpi_name;
	for (i = 0; i < info->dlpi_phnum; i++)
		if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME)
			w->eh_frame = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
	return 0;
}

static int stop_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	(void) info; (void) size; (void) data;
	return 42;
}

static void case_addr_and_phdr(void)
{
	static dl_state st;
	dl_info di;
	void *extra = NULL;
	struct walk_seen w;
	uint64_t base;

	printf("case: dladdr names the symbol, dl_iterate_phdr hands out phdrs\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	build_fake(&st);
	base = st.obj[0].map.base;

	ok(dl_addr(&st, (void *)(uintptr_t)(base + 0x120), &di) != 0,
	   "an address inside the object is found");
	eq_str(di.dli_sname, "alpha", "the containing symbol is named");
	ok(di.dli_saddr == (void *)(uintptr_t)(base + 0x100),
	   "its address is the symbol's, not the query's");
	ok(di.dli_fbase == (void *)(uintptr_t) base, "the load base is reported");

	/* An address in the gap between two sized symbols belongs to neither. */
	ok(dl_addr(&st, (void *)(uintptr_t)(base + 0x300), &di) != 0,
	   "an address in a gap is still inside the object");
	ok(di.dli_sname == NULL, "but no symbol claims it");

	ok(dl_addr(&st, (void *) fake_strs, &di) == 0,
	   "an address outside every object is not found");

	ok(dl_addr1(&st, (void *)(uintptr_t)(base + 0x404), &di, &extra,
	            RTLD_DL_SYMENT) != 0, "dladdr1 answers");
	ok(extra == &fake_syms[2], "RTLD_DL_SYMENT hands back the symbol");
	eq_str(di.dli_sname, "beta", "the second symbol is named");

	extra = NULL;
	dl_addr1(&st, (void *)(uintptr_t)(base + 0x404), &di, &extra,
	         RTLD_DL_LINKMAP);
	ok(extra == &st.obj[0].lm, "RTLD_DL_LINKMAP hands back the link map");

	memset(&w, 0, sizeof w);
	ok(dl_iterate_phdr(&st, walk_cb, &w) == 0, "the walk completes");
	ok(w.n == 1, "it visited the one loaded object");
	ok(w.eh_frame == base + 0x800,
	   "PT_GNU_EH_FRAME is reachable through the phdrs, at the right address");

	ok(dl_iterate_phdr(&st, stop_cb, NULL) == 42,
	   "a callback stops the walk and its value is returned");
}

/* dlinfo answers the questions a program asks an object about itself. */
static void case_dlinfo(void)
{
	static dl_state st;
	struct link_map *lm = NULL;
	char origin[DL_PATH_MAX];
	long lmid = -1;

	printf("case: dlinfo answers about a loaded object\n");
	dl_state_init(&st, NULL, 0, NULL, NULL);
	mk(&st, 0, "plug.so", NULL, NULL);

	ok(dl_info_get(&st, &st.obj[0], RTLD_DI_LINKMAP, &lm) == 0 &&
	   lm == &st.obj[0].lm, "RTLD_DI_LINKMAP");
	ok(dl_info_get(&st, &st.obj[0], RTLD_DI_ORIGIN, origin) == 0, "origin");
	eq_str(origin, "/lib", "the origin is the directory the file came from");
	ok(dl_info_get(&st, &st.obj[0], RTLD_DI_LMID, &lmid) == 0 && lmid == 0,
	   "one namespace");
	ok(dl_info_get(&st, NULL, RTLD_DI_LINKMAP, &lm) == -1,
	   "an invalid handle is refused");
}

int main(void)
{
	printf("WP-38: the dl surface\n");

	case_chain();
	case_diamond();
	case_cycle();
	case_arrays();
	case_error_protocol();
	case_addr_and_phdr();
	case_dlinfo();

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
