# WP-38 — the dl surface

This package is what a running program can ask its loader, and the one thing it
can ask the loader to do: `dlopen`, `dlsym`, `dlvsym`, `dlclose`, `dlerror`,
`dladdr`, `dladdr1`, `dlinfo`, `dl_iterate_phdr`. None of it is a second
implementation of anything. WP-31 parses, WP-32 places, WP-33 walks the
dependency closure, WP-34 relocates, WP-35 resolves, WP-36 versions, WP-37 lays
out TLS, WP-39 announces to a debugger; this package holds the object table
those packages hang off, and decides what to hand each of them.

## The table

One record per loaded object, and a handle is a pointer to one. An object's
identity is its `DT_SONAME`, or the basename of the file it was found at when it
carries none — the same identity WP-33's graph uses, so an object named twice by
two spellings is one object. A second `dlopen` of a loaded object is one more
reference, not a second mapping, and runs no initializer twice.

A `dlopen` of an object with dependencies brings the closure in with it. WP-33
resolves the names to files; the closure is loaded back to front, so every leaf
is in before what needs it; the graph's parent links become the dependency edges
initialization order is computed over; and one relocation pass wires the whole
new group against the world and against itself.

The load and the unload are exact inverses. A load takes a table slot, a file
image, a mapping, a relocation-scope slot and a link-map node; the `dlclose`
that drops the last reference gives all five back, and releases the references
the load took on the closure, so a plugin and its private libraries go away
together. That is not a claim, it is what `t/dl_e2e.c` measures ten thousand
times, checking after every cycle rather than at the end.

Placement is this package's: an object is placed above everything the loader
currently holds, recomputed from the table on each load. With the table empty
that is the floor, so repeated cycles reuse one region rather than walking up
the address space.

## The order

`dl_init.c` is a post-order walk over the dependency edges seeded in load order.
An object is emitted after every dependency it reaches, so `DT_INIT` and
`DT_INIT_ARRAY` run dependencies first; the edge that re-enters an object
already on the walk is the one dropped, which is the cycle tie-break. Finalizing
is the recorded reverse of the order that ran. `DT_PREINIT_ARRAY` is the
program's alone and runs before any `DT_INIT` anywhere. `DT_INIT` precedes
`DT_INIT_ARRAY`; `DT_FINI_ARRAY` runs backwards and then `DT_FINI`.

DR-0025 records why the tie-break is this one and not another.

## The ABI boundary

The loader is compiled for the host, whose C compiler uses the Microsoft x64
convention. The objects it loads were compiled for System V. Every function
pointer that points into a loaded object therefore carries `sysv_abi`, through
one typedef in `dl.h` rather than at each call site, and the compiler emits the
shuffle. The unit test's synthetic initializers carry the same attribute, so a
wrong-register call would fail there rather than waiting for a real object.

## Unwinding

`dl_iterate_phdr` hands out each object's mapped program headers, and
`PT_GNU_EH_FRAME` is a program header. That is the whole of the coupling between
this loader and exception handling: nothing registers anything, and an object
becomes unwindable the moment it is in the table. `dlpi_adds` and `dlpi_subs`
count loads and unloads so an unwinder can tell when its per-object cache is
stale.

## What is not here

`dlmopen` and link-map namespaces. `dlinfo` reports namespace zero and there is
one; the request is answered rather than refused, and a second namespace would
be a change to the table rather than to this interface.

Per-thread `dlerror`. The contract is complete — one report per error, nothing on
the second read — but the carrier is shared until WP-42 delivers threads that
run under this loader.

`RTLD_DEEPBIND`. The flag is accepted and recorded; changing the search order it
implies is a scope decision that belongs with the interposition work, not with
the surface.

## Certification

`t/run.sh` is the whole of it. `t/dl_test.c` builds the object table directly so
that the expected order is written down rather than inferred: a chain, a
diamond, a cycle walked eight times for the same answer, the array ordering, the
`dlerror` protocol, `dladdr` over sized symbols and the gap between them, and the
phdr walk. `t/dl_e2e.c` cross-links `t/plugin.c` into a real shared object with a
constructor, a destructor, an exported function, a relocated pointer and unwind
tables, then runs the done-when: ten thousand loads and unloads that leak
nothing, and `PT_GNU_EH_FRAME` found through `dl_iterate_phdr` in an object that
arrived after startup, at an address whose first byte reads back as the
`.eh_frame_hdr` version.
