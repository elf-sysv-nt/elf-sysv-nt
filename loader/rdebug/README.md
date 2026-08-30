# The rendezvous

A dynamic loader that keeps its objects to itself leaves a debugger blind. The
SVr4 answer, which glibc still implements and which gdb still reads, is a
rendezvous: one structure at an address the debugger can find, a doubly linked
list of the loaded objects that the loader keeps current, and a function the
loader calls after every change so a debugger that put a breakpoint on it learns
the list moved. This package is that rendezvous. It records what the other
loader packages loaded, in the exact shape a debugger reads, and nothing else:
it maps no memory, resolves no relocation, and runs no object.

It is delivered early on purpose. The plan places it the day the loader can
announce a second object, because the alternative is debugging a world no
Windows tool can see, and that cost is paid by every package that comes after.

## What a debugger reads

gdb finds the rendezvous the way SVr4 specifies. The loader plants the address
of the rendezvous structure into the root object's `DT_DEBUG` dynamic entry;
a debugger reading the dynamic section follows that pointer. There is no fixed
numeric address, and `rdebug_plant` is the one call that writes it.

From the structure it reads five fields at fixed byte offsets: the protocol
version, the head of the object list, the address of the breakpoint function,
the state of the list, and the base the loader was mapped at. From each object
it reads five more: the load bias, the path, the dynamic section, and the two
list links. These offsets are the protocol. gdb's `solib-svr4` computes them and
reads target memory directly; a structure a compiler laid out differently would
break a debugger with no diagnostic. So the layout is asserted at compile time
in `rdebug.c` and again, independently, by literal offset in the test.

    struct r_debug (40 bytes)          struct link_map (40 bytes)
      0  r_version   int                 0  l_addr   load bias
      8  r_map       -> link_map         8  l_name   -> path string
     16  r_brk       &_dl_debug_state   16  l_ld     -> dynamic section
     24  r_state     RT_*               24  l_next   -> next object
     32  r_ldbase    loader base        32  l_prev   -> previous object

`r_version` is 1, the base SVr4 protocol. Version 2 -- glibc's `r_debug_extended`
with an `r_next` chain of link-map namespaces -- is a later extension this does
not claim; when a namespace-aware `dlmopen` is built, the version and the
extra field are added then, not guessed at now.

## Keeping the list current

Every change to the list is bracketed. The loader calls `rdebug_map_change_begin`
with `RT_ADD` or `RT_DELETE` before it moves any pointer, which sets `r_state`
and calls the breakpoint function; a debugger stopped there knows the list is
mid-change and does not trust it. The loader then splices or unsplices the node
with `rdebug_map_add` or `rdebug_map_remove`, and calls `rdebug_map_change_end`,
which returns `r_state` to `RT_CONSISTENT` and calls the breakpoint function
again so the debugger re-reads a stable list. Startup is one such bracket around
the whole initial population; each later `dlopen` and `dlclose` is one around
its single object.

The breakpoint function is `_dl_debug_state`. gdb sets its breakpoint at the
address in `r_brk`, which is this function, and also recognises the symbol by
name as a fallback. Its body is empty by construction and it is never inlined,
so the address in `r_brk` names a real instruction a trap can be planted on.

## Wired to the object graph

The initial list is WP-33's walk. `rdebug_populate_from_graph` takes the graph
the object-graph walker produced -- the same objects, in the same breadth-first
load order -- and lifts it into the list, one node per object, the whole
bracketed as one `RT_ADD` change. The graph carries each object's resolved path
and its place in the order but no runtime addresses; those come from the package
that mapped the object (WP-32, WP-34), passed alongside as a small parallel
array of load bias and dynamic-section pointer. The rendezvous pairs the two and
never invents an address it was not given.

## What is certified, and what waits for WP-60

The done-when is a gdb built for the triple listing every object and breaking in
one that arrived through `dlopen`. That gdb does not exist yet; building it is
WP-60. So the live check is deferred there, and what is certified now is
everything that gdb will rely on when it arrives, held to the layout rather than
to memory of it:

  - `r_debug` and `link_map` have the exact SVr4/gdb byte layout, checked by
    `offsetof` against the numbers gdb computes and again by a raw-byte walk of
    the live structures through those same literal offsets;
  - the list is walkable exactly as gdb walks it -- from `r_map`, along
    `l_next`, reading `l_name` -- reconstructing the object names by offset
    arithmetic alone;
  - `r_brk` holds the address of `_dl_debug_state`, and `_dl_debug_state` and
    `_r_debug` carry the names gdb keys on;
  - `r_state` and the chain move correctly through a startup population, a
    `dlopen` add and a `dlclose` remove, including removing the head and a
    middle node, with the breakpoint firing on each transition and the list
    already in its announced state when it does;
  - `DT_DEBUG` planting finds and sets the entry, and reports a missing one;
  - and end to end, a real program cross-linked against a real shared library is
    walked by WP-33, lifted into the rendezvous, and both objects are read back
    through `r_map` in load order -- the loader announcing a second object.

The reason the deferral is honest rather than a gap: the layout the test pins is
the whole of what a debugger consumes, so a gdb that reads it wrong at WP-60
would be wrong against a written, executable statement of the protocol, not
against nothing. The live check adds that gdb in fact reads it; it cannot change
what "it" is.

## Files

  - `rdebug.h` -- the structures, the protocol constants, and the API.
  - `rdebug.c` -- the state machine, the breakpoint function, the compile-time
    layout assertions, and the lift from WP-33's graph.
  - `t/rdebug_test.c` -- the layout-and-transitions unit certification.
  - `t/graph_e2e.c`, `t/root.c`, `t/second.c` -- the end-to-end second-object
    walk and its cross-linked specimens.
  - `t/run.sh` -- the driver; builds both, runs both, reports the verdict.

## Running

    loader/rdebug/t/run.sh

Exit 0 is a pass. The unit test needs only the host compiler; the end-to-end
test needs the cross toolchain on `PATH` to emit the specimens and is skipped,
not failed, where it is absent. The decision behind the shape of the link map is
`doc/decisions/0022-the-rendezvous-link-map.md`.
