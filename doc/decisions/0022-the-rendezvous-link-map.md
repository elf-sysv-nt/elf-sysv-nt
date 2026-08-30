# DR-0022 — the rendezvous link map is the SVr4 five-field prefix, found through DT_DEBUG

Status: accepted
Date: 2026-08-30
Deciding: the loader track, WP-39

## Context

WP-39 delivers the SVr4 `r_debug` rendezvous: the structure a debugger reads to
learn what the loader loaded. Two questions had to be settled before a byte of
it was laid out, because both fix an interface a debugger reads by fixed offset
and neither is cheap to change once a gdb built for the triple (WP-60) depends
on it. What is the shape of a link-map node, given that glibc's own
`struct link_map` is a large private record and the loader will grow a per-object
record of its own? And how does a debugger find the rendezvous — a fixed
address, or a pointer it reads from somewhere?

## What was decided

The gdb-visible link-map node is the five-field SVr4 prefix and only that:
`l_addr`, `l_name`, `l_ld`, `l_next`, `l_prev`, at offsets 0, 8, 16, 24 and 32,
the node forty bytes. This is what gdb's `solib-svr4` reads and the whole of
what it reads; it walks the list by these offsets into target memory and touches
nothing past `l_prev`. The loader's own richer per-object bookkeeping — search
provenance, init state, reference counts, everything WP-34 and WP-38 accumulate
— is a separate structure that may carry a node of this exact type as its head,
so that a pointer to the loader's record is a pointer to the public node, but the
public node's layout is frozen at these five fields regardless of what grows
behind it.

The rendezvous is found the way SVr4 specifies, not at a fixed address. The
loader plants the address of its one `_r_debug` object into the root object's
`DT_DEBUG` dynamic entry, and a debugger reading the dynamic section follows that
pointer. `_r_debug` and the breakpoint function `_dl_debug_state` carry exactly
those names, which gdb also recognises symbolically as a fallback.

The protocol version is 1, the base SVr4 protocol. Version 2 — glibc's
`r_debug_extended`, which adds an `r_next` chain so a debugger can see the
separate link-map namespaces that `dlmopen` creates — is deliberately not
claimed. Claiming a version is a promise to lay out the field that goes with it,
and the field is meaningless until namespaces exist.

## Why

The five-field prefix is not glibc's to define and not ours to embellish; it is
the ABI gdb already reads on every SVr4 system, and its value is that a debugger
built without any knowledge of this loader walks the list correctly. Widening the
public node, or reordering it to match the loader's convenience, would either
break that debugger silently or force a patched gdb — and a patched gdb that has
to know this loader's private layout is precisely the outcome the rendezvous
exists to avoid. Keeping the loader's own state in a separate record that merely
begins with the public node gives the loader all the room it needs without
touching the forty bytes a debugger reads.

Finding the structure through `DT_DEBUG` rather than a fixed address is what lets
the rendezvous live wherever the runtime is mapped, which on this platform is not
negotiable: the runtime is a re-faced Cygwin DLL (DR-0000) mapped by Windows at
an address the loader does not choose. A fixed numeric address would be a fiction
here. `DT_DEBUG` is the indirection SVr4 already defined for exactly this reason,
and gdb already follows it.

Version 1 is the honest claim. The extended protocol answers a question — which
namespace an object belongs to — that does not exist until `dlmopen` and
multiple namespaces do, which is not in WP-39's scope. Announcing version 2 now
would commit the layout to an `r_next` field with nothing to put in it and invite
a debugger to walk a chain of one forever.

## Consequences

WP-60's gdb reads a stock SVr4 rendezvous and needs no knowledge of this
project's internals to list objects or break in a `dlopen`'d one. WP-38's
`dlopen`/`dlclose` maintain the list through the bracketed `begin`/`add` or
`remove`/`end` calls this package exposes, and store their per-object state in
the loader's own record whose head is the public node. When namespace-aware
`dlmopen` is built, the version becomes 2 and the `r_next` field is added then,
in a record that points back at this one; until then the base protocol stands.

## Where it is written down

`loader/rdebug/rdebug.h` and `rdebug.c`, where the layout is fixed and asserted;
`loader/rdebug/README.md`, which describes the shape and the deferral of the live
check to WP-60.
