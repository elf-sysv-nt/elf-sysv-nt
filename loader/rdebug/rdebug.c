/* WP-39: the SVr4 r_debug rendezvous -- implementation.
 *
 * The whole package is bookkeeping in the shape a debugger reads. It maintains
 * one r_debug object, a doubly linked list of link_map nodes hung off r_map,
 * and the discipline that every change to that list is bracketed by a call to
 * the breakpoint function with r_state announcing the change. See rdebug.h for
 * the field contract and README.md for why the shape is what it is.
 */
#include "rdebug.h"

#include "../graph/elf_graph.h"   /* elf_graph, elf_graph_object */

/* The layout is the protocol. These assertions hold the two structures to the
 * offsets and sizes gdb's solib-svr4 reads on LP64; a compiler that laid them
 * out differently would break a debugger silently, so it must fail to build
 * here instead. The numbers are the SVr4/glibc layout, not this file's memory
 * of it -- each is the offset gdb computes for the corresponding field. */
_Static_assert(sizeof(void *) == 8, "the rendezvous layout is LP64-only");

_Static_assert(offsetof(struct link_map, l_addr) == 0,  "l_addr @ 0");
_Static_assert(offsetof(struct link_map, l_name) == 8,  "l_name @ 8");
_Static_assert(offsetof(struct link_map, l_ld)   == 16, "l_ld @ 16");
_Static_assert(offsetof(struct link_map, l_next) == 24, "l_next @ 24");
_Static_assert(offsetof(struct link_map, l_prev) == 32, "l_prev @ 32");
_Static_assert(sizeof(struct link_map) == 40, "link_map is 40 bytes");

_Static_assert(offsetof(struct r_debug, r_version) == 0,  "r_version @ 0");
_Static_assert(offsetof(struct r_debug, r_map)     == 8,  "r_map @ 8");
_Static_assert(offsetof(struct r_debug, r_brk)     == 16, "r_brk @ 16");
_Static_assert(offsetof(struct r_debug, r_state)   == 24, "r_state @ 24");
_Static_assert(offsetof(struct r_debug, r_ldbase)  == 32, "r_ldbase @ 32");
_Static_assert(sizeof(struct r_debug) == 40, "r_debug is 40 bytes");

/* The known address. A debugger reaches it through DT_DEBUG; nothing here
 * depends on where the linker places it. */
struct r_debug _r_debug;

/* NULL in production. The certification test points it at a counter. */
void (*rdebug_brk_observer)(int r_state);

/* The breakpoint function. Empty by construction: its only purpose is to be an
 * address a debugger can trap. Marked noinline and its body kept non-trivial
 * enough not to be elided, so r_brk names a real instruction even at -O2. gdb
 * plants its breakpoint here and reads r_state and r_map when it is hit. */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void _dl_debug_state(void)
{
	/* A volatile touch so the optimiser cannot fold this to nothing and leave
	 * r_brk pointing at a returning stub shared with another empty function. */
	__asm__ __volatile__("" ::: "memory");
}

/* The post-change notifier: call the breakpoint function a debugger watches,
 * then, only when a test installed one, the observer. Keeping _dl_debug_state
 * pristine and the observer in the caller means production carries a single
 * predictable-NULL branch and gdb's breakpoint target is untouched. */
static void rdebug_notify(void)
{
	_dl_debug_state();
	if (rdebug_brk_observer)
		rdebug_brk_observer(_r_debug.r_state);
}

struct r_debug *rdebug_init(Elf64_Addr ldbase)
{
	_r_debug.r_version = R_DEBUG_VERSION;
	_r_debug.r_map     = NULL;
	_r_debug.r_brk     = (Elf64_Addr)(uintptr_t)&_dl_debug_state;
	_r_debug.r_state   = RT_CONSISTENT;
	_r_debug.r_ldbase  = ldbase;
	return &_r_debug;
}

int rdebug_plant(Elf64_Dyn *dyn)
{
	if (!dyn)
		return -1;
	for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
		if (d->d_tag == DT_DEBUG) {
			d->d_un.d_ptr = (Elf64_Addr)(uintptr_t)&_r_debug;
			return 0;
		}
	}
	return -1;
}

void rdebug_map_change_begin(int state)
{
	_r_debug.r_state = state;
	rdebug_notify();
}

void rdebug_map_change_end(void)
{
	_r_debug.r_state = RT_CONSISTENT;
	rdebug_notify();
}

void rdebug_map_add(struct link_map *lm)
{
	if (!lm)
		return;
	lm->l_next = NULL;
	if (!_r_debug.r_map) {
		lm->l_prev = NULL;
		_r_debug.r_map = lm;
		return;
	}
	struct link_map *tail = _r_debug.r_map;
	while (tail->l_next)
		tail = tail->l_next;
	tail->l_next = lm;
	lm->l_prev = tail;
}

void rdebug_map_remove(struct link_map *lm)
{
	if (!lm)
		return;
	if (lm->l_prev)
		lm->l_prev->l_next = lm->l_next;
	else if (_r_debug.r_map == lm)
		_r_debug.r_map = lm->l_next;
	if (lm->l_next)
		lm->l_next->l_prev = lm->l_prev;
	lm->l_next = NULL;
	lm->l_prev = NULL;
}

size_t rdebug_populate_from_graph(const elf_graph *g,
                                  const rdebug_loaded *loaded,
                                  struct link_map *nodes, size_t nodes_cap)
{
	if (!g || !nodes)
		return 0;

	size_t n = g->count;
	if (n > nodes_cap)
		n = nodes_cap;

	rdebug_map_change_begin(RT_ADD);
	for (size_t i = 0; i < n; i++) {
		nodes[i].l_addr = loaded ? loaded[i].l_addr : 0;
		/* The graph owns the path string; the map borrows it and the caller
		 * keeps the graph alive for the map's lifetime. */
		nodes[i].l_name = (char *)g->obj[i].path;
		nodes[i].l_ld   = loaded ? loaded[i].l_ld : NULL;
		rdebug_map_add(&nodes[i]);
	}
	rdebug_map_change_end();
	return n;
}
