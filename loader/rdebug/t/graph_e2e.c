/* WP-39 end-to-end: the loader announces a second object.
 *
 * The plan names the moment this proves -- the day the loader can announce a
 * second object, the rendezvous is wired. A real program is cross-linked
 * against a real shared library; WP-33 walks it; this lifts that walk into the
 * rendezvous and reads the object list back the only way a debugger can: from
 * r_map, then along l_next, reading l_name at each node, all by the byte offsets
 * gdb's solib-svr4 uses. The root and its one library must both appear, in load
 * order. The live gdb check -- a debugger built for the triple sets a breakpoint
 * in a dlopen'd object -- is WP-60, which needs that gdb to exist.
 *
 * Usage: graph_e2e ROOT_PATH
 * Exit 0 both objects announced and walkable; 1 not; 2 usage.
 */
#include "../rdebug.h"
#include "../../graph/elf_graph.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint64_t at_u64(const void *b, size_t o)
{
	uint64_t v;
	memcpy(&v, (const char *)b + o, sizeof v);
	return v;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: graph_e2e ROOT_PATH\n");
		return 2;
	}

	elf_graph_config cfg;
	memset(&cfg, 0, sizeof cfg);
	elf_graph g;
	if (elf_graph_build(argv[1], &cfg, &g) || g.error) {
		fprintf(stderr, "graph walk failed: %s\n", g.errmsg);
		return 1;
	}
	printf("walked %u object(s) from %s\n", g.count, argv[1]);

	static struct link_map nodes[64];
	static rdebug_loaded loaded[64];
	for (unsigned i = 0; i < g.count && i < 64; i++) {
		loaded[i].l_addr = 0x400000ULL + ((uint64_t)i << 20);
		loaded[i].l_ld = NULL;
	}

	rdebug_init(0x400000);
	size_t n = rdebug_populate_from_graph(&g, loaded, nodes, 64);

	/* Walk the map as a debugger does: r_map at r_debug+8, l_name at node+8,
	 * l_next at node+24. */
	int fail = 0;
	unsigned c = 0;
	int found_second = 0;
	const char *head_name = NULL;
	uint64_t node = at_u64(&_r_debug, 8);
	while (node) {
		const char *nm = (const char *)(uintptr_t)at_u64((void *)(uintptr_t)node, 8);
		if (c == 0)
			head_name = nm;
		printf("  r_map[%u] = %s\n", c, nm ? nm : "(null)");
		if (nm && strstr(nm, "libsecond"))
			found_second = 1;
		node = at_u64((void *)(uintptr_t)node, 24);
		c++;
	}

	if (c != n) {
		printf("FAIL: populated %zu nodes but the offset walk sees %u\n", n, c);
		fail = 1;
	}
	if (c < 2) {
		printf("FAIL: fewer than two objects in the map\n");
		fail = 1;
	}
	if (!head_name || strcmp(head_name, g.obj[0].path) != 0) {
		printf("FAIL: the root is not the head of the map\n");
		fail = 1;
	}
	if (!found_second) {
		printf("FAIL: the second object was not announced\n");
		fail = 1;
	}

	elf_graph_free(&g);

	if (!fail)
		printf("rdebug e2e: the loader announced a second object; a debugger "
		       "walking r_map lists both, in load order\n");
	return fail ? 1 : 0;
}
