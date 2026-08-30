/* WP-39: the SVr4 r_debug rendezvous.
 *
 * A dynamic loader that keeps its objects to itself leaves a debugger blind.
 * The SVr4 answer is a rendezvous: one structure at an address the debugger can
 * find, a doubly linked list of loaded objects the loader keeps current, and a
 * function the loader calls after every change so a debugger that breakpointed
 * it learns the list moved. gdb reads exactly these fields at exactly these
 * offsets; this package produces them byte-for-byte so a gdb built for the
 * triple (WP-60) can walk the list and break in an object that arrived through
 * dlopen (WP-38).
 *
 * The list is populated from WP-33's walked object graph at startup -- the same
 * objects, in the same load order -- and kept current one node at a time as
 * dlopen and dlclose add and remove objects afterward. The rendezvous is found
 * the way SVr4 specifies: the loader plants the address of the rendezvous into
 * the root object's DT_DEBUG entry, and a debugger reading the dynamic section
 * follows that pointer. There is no fixed numeric address.
 *
 * Nothing here maps, relocates, or runs an object. It records what the other
 * packages loaded, in the shape a debugger reads. See README.md and
 * doc/decisions/0022-the-rendezvous-link-map.md for why the map is the
 * five-field SVr4 prefix and not the loader's own richer per-object record.
 */
#ifndef ELFSYSV_LOADER_RDEBUG_H
#define ELFSYSV_LOADER_RDEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_types.h"    /* Elf64_Addr, Elf64_Dyn, DT_DEBUG */
#include "../graph/elf_graph.h"  /* elf_graph, the WP-33 object list */

#ifdef __cplusplus
extern "C" {
#endif

/* The base protocol version. gdb reads r_version and refuses a value it does
 * not understand; 1 is the original SVr4 protocol, which is all this delivers.
 * Version 2 (r_debug_extended, with an r_next chain of namespaces) is a later
 * glibc extension and is deliberately not claimed here. */
#define R_DEBUG_VERSION 1

/* The r_state values, read by a debugger out of r_debug.r_state to know whether
 * the map is safe to walk. Their numeric values are part of the protocol -- gdb
 * compares the word at r_state's offset against these constants -- so they are
 * fixed at 0/1/2 and must never be reordered or renumbered. */
enum {
	RT_CONSISTENT = 0,  /* the map is stable and safe for a debugger to read */
	RT_ADD        = 1,  /* an object is being added; the map is mid-change */
	RT_DELETE     = 2   /* an object is being removed; the map is mid-change */
};

/* One node of the gdb-visible link map: the five-field SVr4 prefix, and only
 * that. On LP64 the fields sit at offsets 0, 8, 16, 24, 32 and the node is 40
 * bytes; gdb's solib-svr4 reads l_addr, l_name, l_ld, l_next and l_prev at
 * those offsets and reads nothing past them. The loader's own per-object state
 * lives elsewhere and may embed a node of this type as its head; see DR-0022. */
struct link_map {
	Elf64_Addr        l_addr;   /* load bias: runtime address - link-time one */
	char             *l_name;   /* absolute path the object was found in */
	Elf64_Dyn        *l_ld;     /* the object's dynamic section, in memory */
	struct link_map  *l_next;   /* next loaded object, NULL at the tail */
	struct link_map  *l_prev;   /* previous object, NULL at the head (root) */
};

/* The rendezvous structure. On LP64 r_version sits at 0, r_map at 8, r_brk at
 * 16, r_state at 24 and r_ldbase at 32, and the whole is 40 bytes. r_state is
 * declared int rather than the anonymous enum glibc uses so its width is a
 * fixed four bytes regardless of how a compiler sizes that enum; the value is
 * one of the RT_* constants. */
struct r_debug {
	int              r_version;  /* protocol version; R_DEBUG_VERSION */
	struct link_map *r_map;      /* head of the link_map chain (the root) */
	Elf64_Addr       r_brk;      /* address of the breakpoint function */
	int              r_state;    /* RT_CONSISTENT / RT_ADD / RT_DELETE */
	Elf64_Addr       r_ldbase;   /* base address the loader was mapped at */
};

/* The one rendezvous object, defined once in rdebug.c. Its address is the known
 * address: rdebug_plant writes &_r_debug into the root's DT_DEBUG entry, and a
 * debugger finds it there. */
extern struct r_debug _r_debug;

/* The breakpoint function. gdb sets a breakpoint at the address in r_brk, which
 * is &_dl_debug_state, and also recognises this symbol by name as its fallback
 * breakpoint target. The loader calls it after every change to the map, twice
 * per change: once on entering a mid-change state and once on returning to
 * RT_CONSISTENT. Its body is empty by construction and it is never inlined, so
 * the address in r_brk names a real instruction a trap can be planted on. */
void _dl_debug_state(void);

/* A test observability seam, not part of the protocol. When non-NULL it is
 * called just after _dl_debug_state on every transition, passed the r_state a
 * debugger would then read. It is NULL in production, where a transition's only
 * outward effect is the empty _dl_debug_state call gdb breakpoints; only the
 * certification test sets it, to prove the breakpoint fires on every change and
 * with the map already in its announced state. */
extern void (*rdebug_brk_observer)(int r_state);

/* Initialise the rendezvous: r_version = R_DEBUG_VERSION, r_brk =
 * &_dl_debug_state, r_ldbase = ldbase, an empty map, r_state = RT_CONSISTENT.
 * Idempotent -- calling it again resets the same fields and empties the map.
 * Returns &_r_debug, the address to plant in DT_DEBUG. */
struct r_debug *rdebug_init(Elf64_Addr ldbase);

/* Plant &_r_debug into the DT_DEBUG entry of a root object's in-memory dynamic
 * array `dyn` (NULL-terminated by a DT_NULL entry). Returns 0 when a DT_DEBUG
 * entry was present and set, -1 when the array carries none. */
int rdebug_plant(Elf64_Dyn *dyn);

/* Announce that the map is about to change: set r_state to `state` (RT_ADD or
 * RT_DELETE) and call the breakpoint function, so a debugger notes the map is
 * mid-change before any pointer moves. */
void rdebug_map_change_begin(int state);

/* Splice `lm` onto the tail of the map; its l_* fields must already be filled.
 * Maintains l_next/l_prev and sets r_map when the map was empty. Call between
 * rdebug_map_change_begin(RT_ADD) and rdebug_map_change_end. */
void rdebug_map_add(struct link_map *lm);

/* Unsplice `lm` from the map, repairing its neighbours and r_map. Call between
 * rdebug_map_change_begin(RT_DELETE) and rdebug_map_change_end. */
void rdebug_map_remove(struct link_map *lm);

/* Announce the change is complete: set r_state = RT_CONSISTENT, then call the
 * breakpoint function so a debugger re-reads a now-stable map. */
void rdebug_map_change_end(void);

/* The runtime address of one loaded object, paired with a WP-33 graph node to
 * fill a link_map. WP-33's graph carries the resolved path and load order but
 * no runtime addresses; the package that mapped the object (WP-32/WP-34) knows
 * the bias and the in-memory dynamic section. This pairs the two. */
typedef struct {
	Elf64_Addr  l_addr;   /* the object's runtime load bias */
	Elf64_Dyn  *l_ld;     /* its dynamic section, in memory */
} rdebug_loaded;

/* Wire WP-33's walked graph into the map as the startup population. For each of
 * the graph's `count` objects, in load order, fill nodes[i] -- l_name from the
 * graph's resolved path, l_addr and l_ld from loaded[i] -- and splice it on,
 * the whole bracketed by one begin(RT_ADD)/end pair. `nodes` and the strings
 * they point at must outlive the map. Returns the number of nodes linked, which
 * is min(graph count, nodes_cap). */
size_t rdebug_populate_from_graph(const elf_graph *g,
                                  const rdebug_loaded *loaded,
                                  struct link_map *nodes, size_t nodes_cap);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_RDEBUG_H */
