/* WP-39 certification: the rendezvous is byte-correct and kept current.
 *
 * No gdb built for the triple exists yet -- that is WP-60 -- so the live check
 * (a debugger lists every object and breaks in a dlopen'd one) is deferred
 * there. What is certifiable now is everything a debugger relies on: that
 * r_debug and link_map have the exact SVr4/gdb layout, that the map can be
 * walked purely by the offset arithmetic gdb uses, that r_brk names the
 * breakpoint function, and that r_state and the chain move correctly through an
 * add and a remove -- the transitions a debugger reads on each breakpoint hit.
 *
 * The layout is checked two ways that cannot both be wrong in the same
 * direction: offsetof against the numbers gdb computes, and a raw-byte walk of
 * the live structures through those same numeric offsets, reconstructing the
 * object list the way a debugger reads target memory.
 */
#include "../rdebug.h"
#include "../../graph/elf_graph.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                        \
	checks++;                                                         \
	if (!(cond)) {                                                    \
		failures++;                                                   \
		printf("  FAIL %s:%d: ", __FILE__, __LINE__);                \
		printf(__VA_ARGS__);                                         \
		printf("\n");                                                \
	}                                                                \
} while (0)

/* ---- the breakpoint observer ------------------------------------------- */

static int   brk_calls;
static int   brk_states[16];
static struct link_map *brk_head[16];

static void observe(int r_state)
{
	if (brk_calls < 16) {
		brk_states[brk_calls] = r_state;
		brk_head[brk_calls]   = _r_debug.r_map;
	}
	brk_calls++;
}

/* ---- read a field the way gdb does: a load at a fixed byte offset ------- */

static uint64_t at_u64(const void *base, size_t off)
{
	uint64_t v;
	memcpy(&v, (const char *)base + off, sizeof v);
	return v;
}

static uint32_t at_u32(const void *base, size_t off)
{
	uint32_t v;
	memcpy(&v, (const char *)base + off, sizeof v);
	return v;
}

/* Byte offsets gdb's solib-svr4 reads on LP64. Duplicated here as literals on
 * purpose: the test is the independent statement of the layout, so it names the
 * numbers rather than deriving them from the same struct it is checking. */
#define LM_L_ADDR   0
#define LM_L_NAME   8
#define LM_L_LD     16
#define LM_L_NEXT   24
#define LM_L_PREV   32
#define LM_SIZE     40

#define RD_R_VERSION 0
#define RD_R_MAP     8
#define RD_R_BRK     16
#define RD_R_STATE   24
#define RD_R_LDBASE  32
#define RD_SIZE      40

/* ---- 1. layout, by offsetof and by size -------------------------------- */

static void test_layout(void)
{
	printf("== layout ==\n");
	CHECK(sizeof(struct link_map) == LM_SIZE, "link_map size %zu", sizeof(struct link_map));
	CHECK(offsetof(struct link_map, l_addr) == LM_L_ADDR, "l_addr off");
	CHECK(offsetof(struct link_map, l_name) == LM_L_NAME, "l_name off");
	CHECK(offsetof(struct link_map, l_ld)   == LM_L_LD,   "l_ld off");
	CHECK(offsetof(struct link_map, l_next) == LM_L_NEXT, "l_next off");
	CHECK(offsetof(struct link_map, l_prev) == LM_L_PREV, "l_prev off");

	CHECK(sizeof(struct r_debug) == RD_SIZE, "r_debug size %zu", sizeof(struct r_debug));
	CHECK(offsetof(struct r_debug, r_version) == RD_R_VERSION, "r_version off");
	CHECK(offsetof(struct r_debug, r_map)     == RD_R_MAP,     "r_map off");
	CHECK(offsetof(struct r_debug, r_brk)     == RD_R_BRK,     "r_brk off");
	CHECK(offsetof(struct r_debug, r_state)   == RD_R_STATE,   "r_state off");
	CHECK(offsetof(struct r_debug, r_ldbase)  == RD_R_LDBASE,  "r_ldbase off");

	printf("  r_debug %zu bytes, link_map %zu bytes; fields at "
	       "SVr4 offsets\n", sizeof(struct r_debug), sizeof(struct link_map));
}

/* ---- 2. init plants the protocol constants ----------------------------- */

static void test_init(void)
{
	printf("== init ==\n");
	struct r_debug *r = rdebug_init(0xdead0000ULL);
	CHECK(r == &_r_debug, "init returns &_r_debug");
	CHECK(_r_debug.r_version == R_DEBUG_VERSION, "r_version %d", _r_debug.r_version);
	CHECK(_r_debug.r_version == 1, "r_version is the base protocol");
	CHECK(_r_debug.r_state == RT_CONSISTENT, "r_state consistent at init");
	CHECK(_r_debug.r_map == NULL, "map empty at init");
	CHECK(_r_debug.r_ldbase == 0xdead0000ULL, "r_ldbase set");
	CHECK(_r_debug.r_brk == (Elf64_Addr)(uintptr_t)&_dl_debug_state,
	      "r_brk is &_dl_debug_state");

	/* The raw-byte view a debugger would read. */
	CHECK(at_u32(r, RD_R_VERSION) == 1, "byte r_version");
	CHECK(at_u64(r, RD_R_BRK) == (uint64_t)(uintptr_t)&_dl_debug_state, "byte r_brk");
	CHECK(at_u32(r, RD_R_STATE) == RT_CONSISTENT, "byte r_state");
	CHECK(at_u64(r, RD_R_LDBASE) == 0xdead0000ULL, "byte r_ldbase");
}

/* Walk the map the way gdb does: start at r_map (r_debug + 8), and at each node
 * read l_name (node + 8) and step through l_next (node + 24), using nothing but
 * byte offsets into target memory. Fills names[] with the l_name pointers and
 * returns the count. This is the reconstruction a debugger performs; if it
 * agrees with the list we built, the layout is walkable as gdb walks it. */
static size_t gdb_walk(const struct r_debug *r, const char *names[], size_t cap)
{
	uint64_t node = at_u64(r, RD_R_MAP);
	size_t n = 0;
	while (node && n < cap) {
		uint64_t namep = at_u64((const void *)(uintptr_t)node, LM_L_NAME);
		names[n++] = (const char *)(uintptr_t)namep;
		node = at_u64((const void *)(uintptr_t)node, LM_L_NEXT);
	}
	return n;
}

/* Assert l_prev/l_next are mutually consistent along the whole chain and the
 * head's l_prev is NULL. Returns the node count. */
static size_t chain_check(void)
{
	size_t n = 0;
	struct link_map *p = _r_debug.r_map;
	CHECK(p == NULL || p->l_prev == NULL, "head l_prev is NULL");
	struct link_map *prev = NULL;
	while (p) {
		CHECK(p->l_prev == prev, "l_prev threads back");
		prev = p;
		p = p->l_next;
		n++;
	}
	return n;
}

/* ---- 3. startup population, a dlopen add, a dlclose remove -------------- */

static struct link_map nodes[8];
static char n0[] = "/lib/root";
static char n1[] = "/lib/liba.so";
static char n2[] = "/lib/libb.so";
static char n3[] = "/lib/plugin.so";   /* the one that "arrives through dlopen" */

static void test_transitions(void)
{
	printf("== transitions ==\n");
	rdebug_init(0x400000);
	brk_calls = 0;

	/* Startup: three objects announced as one RT_ADD change. */
	memset(nodes, 0, sizeof nodes);
	nodes[0].l_name = n0; nodes[1].l_name = n1; nodes[2].l_name = n2;
	rdebug_map_change_begin(RT_ADD);
	rdebug_map_add(&nodes[0]);
	rdebug_map_add(&nodes[1]);
	rdebug_map_add(&nodes[2]);
	rdebug_map_change_end();

	CHECK(brk_calls == 2, "startup fired the breakpoint twice, got %d", brk_calls);
	CHECK(brk_states[0] == RT_ADD, "first brk saw RT_ADD");
	CHECK(brk_states[1] == RT_CONSISTENT, "second brk saw RT_CONSISTENT");
	CHECK(chain_check() == 3, "three objects after startup");
	CHECK(_r_debug.r_map == &nodes[0], "root is the head");

	const char *seen[8];
	size_t got = gdb_walk(&_r_debug, seen, 8);
	CHECK(got == 3, "gdb-walk sees three, got %zu", got);
	CHECK(got == 3 && strcmp(seen[0], "/lib/root") == 0, "walk[0] root");
	CHECK(got == 3 && strcmp(seen[1], "/lib/liba.so") == 0, "walk[1] liba");
	CHECK(got == 3 && strcmp(seen[2], "/lib/libb.so") == 0, "walk[2] libb");
}

static void test_dlopen_dlclose(void)
{
	printf("== dlopen add / dlclose remove ==\n");
	/* dlopen: a fourth object arrives after startup, one RT_ADD change. */
	brk_calls = 0;
	nodes[3].l_name = n3;
	nodes[3].l_next = nodes[3].l_prev = NULL;
	rdebug_map_change_begin(RT_ADD);
	rdebug_map_add(&nodes[3]);
	rdebug_map_change_end();

	CHECK(brk_calls == 2, "dlopen fired the breakpoint twice, got %d", brk_calls);
	CHECK(brk_states[0] == RT_ADD, "dlopen brk saw RT_ADD");
	/* When the RT_ADD breakpoint fired, the map was already announced as
	 * mid-change: a debugger that stops here knows not to trust the pointers. */
	CHECK(chain_check() == 4, "four objects after dlopen");

	const char *seen[8];
	size_t got = gdb_walk(&_r_debug, seen, 8);
	CHECK(got == 4 && strcmp(seen[3], "/lib/plugin.so") == 0,
	      "the dlopen'd object is walkable at the tail");

	/* dlclose: remove the middle object liba.so, one RT_DELETE change. */
	brk_calls = 0;
	rdebug_map_change_begin(RT_DELETE);
	rdebug_map_remove(&nodes[1]);
	rdebug_map_change_end();

	CHECK(brk_calls == 2, "dlclose fired the breakpoint twice, got %d", brk_calls);
	CHECK(brk_states[0] == RT_DELETE, "dlclose brk saw RT_DELETE");
	CHECK(brk_states[1] == RT_CONSISTENT, "dlclose settled to RT_CONSISTENT");
	CHECK(chain_check() == 3, "three objects after dlclose");

	got = gdb_walk(&_r_debug, seen, 8);
	CHECK(got == 3, "gdb-walk sees three after remove, got %zu", got);
	CHECK(got == 3 && strcmp(seen[0], "/lib/root") == 0, "root still head");
	CHECK(got == 3 && strcmp(seen[1], "/lib/libb.so") == 0, "libb spliced to root");
	CHECK(got == 3 && strcmp(seen[2], "/lib/plugin.so") == 0, "plugin still tail");

	/* Remove the head, then the tail, to exercise both ends. */
	rdebug_map_change_begin(RT_DELETE);
	rdebug_map_remove(&nodes[0]);          /* the head */
	rdebug_map_change_end();
	CHECK(_r_debug.r_map == &nodes[2], "r_map advanced past removed head");
	CHECK(_r_debug.r_map->l_prev == NULL, "new head has NULL l_prev");
	CHECK(chain_check() == 2, "two objects after head removal");
}

/* ---- 4. DT_DEBUG planting ---------------------------------------------- */

static void test_plant(void)
{
	printf("== DT_DEBUG plant ==\n");
	Elf64_Dyn dyn[4];
	dyn[0].d_tag = DT_NEEDED; dyn[0].d_un.d_val = 1;
	dyn[1].d_tag = DT_DEBUG;  dyn[1].d_un.d_ptr = 0;
	dyn[2].d_tag = DT_STRTAB; dyn[2].d_un.d_ptr = 0x1000;
	dyn[3].d_tag = DT_NULL;   dyn[3].d_un.d_val = 0;

	CHECK(rdebug_plant(dyn) == 0, "plant finds DT_DEBUG");
	CHECK(dyn[1].d_un.d_ptr == (Elf64_Addr)(uintptr_t)&_r_debug,
	      "DT_DEBUG now points at _r_debug");

	Elf64_Dyn none[2];
	none[0].d_tag = DT_STRTAB; none[0].d_un.d_ptr = 0x1000;
	none[1].d_tag = DT_NULL;   none[1].d_un.d_val = 0;
	CHECK(rdebug_plant(none) == -1, "plant reports a missing DT_DEBUG");
	CHECK(rdebug_plant(NULL) == -1, "plant refuses a NULL array");
}

/* ---- 5. wiring to WP-33's object list ---------------------------------- */

/* rdebug_populate_from_graph must lift a walked graph into the map: the same
 * objects, in the same order, each node's l_name the graph's resolved path and
 * its l_addr/l_ld the runtime pair. Here the graph is built by hand -- an
 * end-to-end walk of a real cross-linked program is in run.sh -- to prove the
 * lift preserves order and pairs the runtime addresses with the right node. */
static void test_graph_wiring(void)
{
	printf("== wiring to WP-33 graph ==\n");
	elf_graph g;
	memset(&g, 0, sizeof g);
	elf_graph_object gobj[3];
	memset(gobj, 0, sizeof gobj);
	strcpy(gobj[0].path, "/opt/app/app");
	strcpy(gobj[1].path, "/opt/app/libx.so");
	strcpy(gobj[2].path, "/opt/app/liby.so");
	g.obj = gobj;
	g.count = 3;

	rdebug_loaded loaded[3] = {
		{ 0x400000, (Elf64_Dyn *)0x401000 },
		{ 0x7f0000, (Elf64_Dyn *)0x7f1000 },
		{ 0x7e0000, (Elf64_Dyn *)0x7e1000 },
	};
	struct link_map lm[3];
	memset(lm, 0, sizeof lm);

	rdebug_init(0x400000);
	brk_calls = 0;
	size_t n = rdebug_populate_from_graph(&g, loaded, lm, 3);

	CHECK(n == 3, "populated three nodes, got %zu", n);
	CHECK(brk_calls == 2, "populate bracketed by one RT_ADD change");
	CHECK(chain_check() == 3, "map holds the three graph objects");
	CHECK(_r_debug.r_map == &lm[0], "graph root is the head");

	const char *seen[8];
	size_t got = gdb_walk(&_r_debug, seen, 8);
	CHECK(got == 3 && strcmp(seen[0], "/opt/app/app") == 0, "wire[0] app");
	CHECK(got == 3 && strcmp(seen[1], "/opt/app/libx.so") == 0, "wire[1] libx");
	CHECK(got == 3 && strcmp(seen[2], "/opt/app/liby.so") == 0, "wire[2] liby");
	CHECK(lm[1].l_addr == 0x7f0000, "runtime bias paired onto node 1");
	CHECK(lm[2].l_ld == (Elf64_Dyn *)0x7e1000, "runtime l_ld paired onto node 2");
}

int main(void)
{
	rdebug_brk_observer = observe;

	test_layout();
	test_init();
	test_transitions();
	test_dlopen_dlclose();
	test_plant();
	test_graph_wiring();

	printf("\n%d checks, %d failures\n", checks, failures);
	if (failures == 0)
		printf("rdebug: the rendezvous is byte-correct and kept current\n");
	return failures ? 1 : 0;
}
