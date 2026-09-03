# DR-0025 — initialization order, the cycle tie-break, and calling into a loaded object

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: the loader track, WP-38

## Context

WP-38 delivers the dl surface, and with it the first place the loader calls
code it loaded rather than merely arranging it. Three questions had to be
settled before that call could be written, and each of them is visible to
programs in a way that is expensive to change later.

The first is what order constructors run in when the dependency graph has a
cycle. Dependencies before dependents determines the order completely for an
acyclic graph, and real programs are not acyclic: two libraries that reference
each other are common enough that a loader without an answer produces a
different order on different runs, and a program that works or crashes
depending on which run it was.

The second is how an object loaded after startup gets its TLS. The static TLS
block is sized once, from the modules present when the first thread's block is
allocated; a module that arrives through `dlopen` cannot be added to a layout
that already exists behind live thread pointers.

The third is the calling convention. The loader is compiled for the host side,
where the C compiler uses the Microsoft x64 convention. The objects it loads
were compiled for System V. The two disagree about which registers carry
arguments, which registers a callee must preserve, and whether there is a red
zone. A constructor called through an ordinary function pointer would receive
its arguments in the wrong registers, and would do so silently.

## What was decided

Initialization order is a post-order depth-first walk over the dependency
edges, seeded in load order. An object is emitted after every dependency it can
reach; an edge that re-enters an object already on the walk is the edge that
closes a cycle, and that single edge is dropped. Dropping the closing edge
rather than the whole cycle keeps every constraint that is not part of the
cycle intact, and seeding the walk in load order makes which edge closes the
cycle a property of the link order the program was built with. The order is
therefore the same on every run of the same program, and finalization is the
recorded reverse of the order that actually ran rather than a second
computation over a graph that may have changed under a later `dlopen`.

An object that arrives after startup is marked late in the relocation scope and
is left out of the static TLS layout entirely. It receives a dynamic module id
through WP-37's `elf_tls_add_dynamic`, and its block is allocated lazily in each
thread that touches it. The static block's running size and next module id
became scope state rather than locals so that a later relocation pass continues
the layout instead of restarting it.

Every function pointer that points into a loaded object carries
`__attribute__((sysv_abi))`, through a single typedef declared in `dl.h` rather
than at each call site. The compiler then emits the register shuffle at the
boundary. The certification's synthetic initializers carry the same attribute,
so the unit cases exercise the pointer type the loader really calls through
rather than a host-ABI stand-in that would hide a wrong-register call until a
real object was loaded.

Relocation became incremental to support all of this. An object is marked
applied once relocated and skipped on a later pass, so a `dlopen` relocates only
what it brought in while still resolving against everything already loaded.
This is not an optimization: RELR adds the load bias into each slot it names and
cannot be applied twice, so a second unconditional pass over a grown scope would
corrupt every object already in it.

## Why, and what was given up

The cycle tie-break could have been left to whatever a traversal happened to
produce, which costs nothing to write and is what a loader that has never
thought about it does. The cost lands on the program author instead, as a bug
that appears on one machine and not another. It could also have been made an
error — refusing to load a graph with a cycle — which is defensible and which
no existing program is built against, so it would break working software for a
principle.

Placement of a `dlopen`'d object is computed from the object table on each load
rather than from a running cursor. A cursor is simpler and is what a loader that
never unloads anything would use; it also means ten thousand load and unload
cycles walk ten thousand mappings up the address space, which is exactly the
case the done-when measures. Recomputing puts a plugin back at the same address
every cycle when the table is otherwise empty.

The reference model is the simple one: a `dlopen` takes a reference on the
object and on every dependency it brought in or joined, and the matching
`dlclose` releases all of them. This unloads a plugin's private libraries with
it, which is what a program expects. It also means an object held only by a
plugin goes away when the plugin does, even if some other code has cached a
pointer into it — glibc has the same exposure, and the answer for a program that
needs otherwise is `RTLD_NODELETE`, which is honored.

`dlerror` is state on the loader rather than per-thread state. The contract it
delivers — one report per error, nothing on the second read — is complete, but
the carrier is shared. Making it per-thread requires the thread-local storage a
thread actually running under this loader will have, which is WP-42's work; the
alternative was to fake a thread-local now and rework it there.
