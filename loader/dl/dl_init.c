/* WP-38: initialization order.
 *
 * The rule an ELF program is written against is that an object's constructors
 * run after the constructors of everything it depends on, and its destructors
 * run before theirs. A dependency graph without cycles determines that order
 * completely. A graph with cycles does not, and real programs have them, so the
 * loader has to choose -- and the choice has to be the same choice every run,
 * or a program's behaviour depends on the order a hash table happened to be in.
 *
 * The order here is a post-order depth-first walk over the dependency edges,
 * seeded in load order. An object is emitted after every dependency it can
 * reach has been emitted; an edge that re-enters an object already on the walk
 * is the edge that closes a cycle, and it is dropped. Dropping the closing edge
 * rather than the whole cycle keeps every non-cyclic constraint intact, and
 * seeding in load order makes which edge closes the cycle a property of the
 * link order the program was built with -- deterministic, and the same
 * tie-break glibc's own traversal arrives at.
 *
 * Finalization is the reverse of the order that ran, recorded rather than
 * recomputed, so an object whose initializer never ran never has its finalizer
 * run either, and a graph that changed under a dlopen does not reorder the
 * teardown of what was already up.
 */
#include <string.h>

#include "dl.h"

/* Read one dynamic tag out of an object's mapped dynamic array. Returns 1 with
 * *out set, or 0 when the array carries no such tag. */
static int dyn_val(const dl_object *o, int64_t tag, uint64_t *out)
{
	const Elf64_Dyn *d;
	if (!o->dyn)
		return 0;
	for (d = o->dyn; d->d_tag != DT_NULL; d++) {
		if (d->d_tag == tag) {
			*out = d->d_un.d_val;
			return 1;
		}
	}
	return 0;
}

/* A dynamic entry holding an address, biased into the mapped image. Returns 0
 * when the tag is absent or the address is null. */
static uint64_t dyn_addr(const dl_object *o, int64_t tag)
{
	uint64_t v;
	if (!dyn_val(o, tag, &v) || v == 0)
		return 0;
	return o->map.load_bias + v;
}

/* ---- the order --------------------------------------------------------- */

struct walk {
	const dl_state *st;
	unsigned *out;
	unsigned  cap;
	unsigned  n;
	unsigned char state[DL_MAX_OBJECTS];  /* 0 unseen, 1 on the walk, 2 done */
	unsigned char wanted[DL_MAX_OBJECTS]; /* in the caller's set */
};

static void visit(struct walk *w, unsigned slot)
{
	const dl_object *o;
	unsigned i;

	if (slot >= DL_MAX_OBJECTS || w->state[slot] != 0)
		return;                  /* done, or the edge that closes a cycle */
	w->state[slot] = 1;

	o = &w->st->obj[slot];
	for (i = 0; i < o->dep_count; i++)
		if (w->wanted[o->dep[i]])
			visit(w, o->dep[i]);

	w->state[slot] = 2;
	if (w->n < w->cap)
		w->out[w->n++] = slot;
}

unsigned dl_init_order(const dl_state *st, const unsigned *slots, unsigned n,
                       unsigned *out, unsigned cap)
{
	static struct walk w;    /* the table is large; keep it off the stack */
	unsigned i;

	if (!st || !slots || !out || cap == 0)
		return 0;

	memset(&w, 0, sizeof w);
	w.st = st;
	w.out = out;
	w.cap = cap;

	for (i = 0; i < n; i++)
		if (slots[i] < DL_MAX_OBJECTS)
			w.wanted[slots[i]] = 1;

	for (i = 0; i < n; i++)
		if (slots[i] < DL_MAX_OBJECTS)
			visit(&w, slots[i]);

	return w.n;
}

/* ---- running them ------------------------------------------------------ */

/* Call one function through the argc/argv/envp calling convention DT_INIT and
 * the array entries share on this platform. */
static void call_init(const dl_state *st, uint64_t addr)
{
	dl_init_fn fn = (dl_init_fn)(uintptr_t) addr;
	fn(st->argc, st->argv, st->envp);
}

static void run_array(const dl_state *st, const dl_object *o,
                      int64_t arr_tag, int64_t sz_tag, int reverse)
{
	uint64_t base = dyn_addr(o, arr_tag);
	uint64_t bytes = 0;
	uint64_t count, i;

	if (!base || !dyn_val(o, sz_tag, &bytes) || bytes < sizeof(uint64_t))
		return;
	count = bytes / sizeof(uint64_t);

	for (i = 0; i < count; i++) {
		uint64_t slot = reverse ? count - 1 - i : i;
		uint64_t fn = ((const uint64_t *)(uintptr_t) base)[slot];
		/* A null or all-ones entry is the linker's placeholder in a section
		 * that was padded; both are skipped rather than called. */
		if (fn != 0 && fn != (uint64_t) -1)
			call_init(st, fn);
	}
}

unsigned dl_run_init(dl_state *st, const unsigned *slots, unsigned n)
{
	unsigned i, ran = 0;

	if (!st || !slots)
		return 0;

	for (i = 0; i < n; i++) {
		dl_object *o;
		if (slots[i] >= DL_MAX_OBJECTS)
			continue;
		o = &st->obj[slots[i]];
		if (!o->in_use || o->initialized)
			continue;

		/* DT_PREINIT_ARRAY is the program's alone -- the specification says
		 * it is ignored in a shared object -- and it runs before any
		 * DT_INIT anywhere, so it is taken from the startup root before the
		 * per-object loop reaches anything. */
		if (o->is_startup && o->slot == 0)
			run_array(st, o, DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ, 0);

		/* DT_INIT before DT_INIT_ARRAY: the legacy _init runs first, which is
		 * what the ABI says and what a mixed-vintage object set expects. */
		{
			uint64_t init = dyn_addr(o, DT_INIT);
			if (init)
				call_init(st, init);
		}
		run_array(st, o, DT_INIT_ARRAY, DT_INIT_ARRAYSZ, 0);

		o->initialized = 1;
		ran++;
	}
	return ran;
}

void dl_run_fini(dl_state *st, dl_object *o)
{
	uint64_t fini;

	if (!st || !o || !o->in_use || !o->initialized)
		return;

	/* The exact reverse of initialization: the array backwards, then DT_FINI. */
	run_array(st, o, DT_FINI_ARRAY, DT_FINI_ARRAYSZ, 1);
	fini = dyn_addr(o, DT_FINI);
	if (fini)
		call_init(st, fini);

	o->initialized = 0;
}
