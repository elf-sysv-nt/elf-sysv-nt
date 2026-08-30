# loader/lookup — WP-35, symbol lookup

This is the resolver the loader asks a name of. WP-33 found the objects a
program needs and put them in load order; WP-34 relocated them with the least
symbol resolution a relocation can stand on, a linear first-definition scan of
the scope. This package is the general answer that scan deferred to: given the
reference `printf` in some object, the definition a real `ld.so` would bind it
to. It carries the three things the scan did not — the hash tables that make a
lookup a short probe rather than a walk of every symbol, the scope ordering that
decides which objects are searched and in what order, and the interposition rule
that makes `LD_PRELOAD` mean here what it means on Linux.

## What it is

`elf_hash.c` is the arithmetic of the two hash tables every dynamic object
carries and the chain walk over them. `elf_sysv_hash` and `elf_gnu_hash` are the
two hash functions; `elf_object_find` probes one object for a name, using
`.gnu.hash` when the object has one and falling back to `.hash` and then to a
linear scan. The GNU path tests the Bloom filter before it touches a chain and
walks the chain by the stolen-low-bit end marker; the SysV path walks its bucket
chain with a bound and a self-loop guard so a malformed table cannot spin. Both
skip a candidate that is a reference rather than a definition, that is local and
so invisible across objects, whose type is not one a lookup binds to, or whose
version a matcher rejects.

`elf_lookup.c` decides which objects are asked and in what order, and applies the
binding rule across the whole search list. A scope is a plain ordered list of
objects; `elf_scope_build_global` writes the one order that matters — the main
object, then the `LD_PRELOAD` interposers, then the breadth-first dependency
closure — and `elf_lookup` searches the global scope, then the reference's own
local dependency list, then any `RTLD_GLOBAL` `dlopen` additions, in that order.
Across the concatenation the rule is glibc's observable one: the first global
(or GNU-unique) definition wins outright and ends the search, while a weak
definition is remembered but does not, so a global reached later still overrides
an earlier weak, and only with no global anywhere does the first weak win.

Interposition is not a rule of its own here; it is a consequence of that order.
A definition reached earlier in the search list wins, so an object placed right
after the main one — which is exactly where the preloads go — shadows the same
name in a regular dependency. That is the whole of what `LD_PRELOAD` does.

## The seam to versioning

WP-36 is versioned lookup, and it sits directly on this package through one
seam: the `elf_version_matcher` a caller may pass to `elf_object_find` and
`elf_lookup`. WP-35 passes none, which is unversioned lookup — every visible
definition is accepted and the first-found rule stands. WP-36 will pass a
matcher that reads `.gnu.version` at the candidate's index against the
referencing object's verneed and reports whether the candidate satisfies the
request and whether it is the default (`@@`) binding; a candidate the matcher
rejects is skipped and the chain walk continues past it. Nothing else in this
interface moves when versioning arrives, which is why the seam is drawn here and
not left for WP-36 to cut through the resolver.

## Its relation to WP-34's resolver

WP-34 keeps its own small first-definition scan, and deliberately: it is the
bootstrap subset a relocation needs before a full scope exists, and its header
draws that line. This package is the general engine the version matcher (WP-36)
and the `dl` surface (WP-38) build on. The two agree on the binding rule; a
later consolidation could point WP-34's relocation at this resolver once the
scope it needs is available at relocation time, but that is not required for
either to be correct and is not done here.

## Why it is written from the specification

The hash tables and the resolution order are the generic ABI's and Drepper's,
not glibc's. glibc's resolver is LGPL and assumes a kernel this platform does
not have (DR-0000, DR-0004), so it is read for its observable behaviour and
written afresh, the same discipline WP-31 and WP-34 followed with their own
structures rather than a host `<elf.h>`. DR-0019 records the load-bearing
choices: a separate engine rather than a rewrite of WP-34's scan, the three-
stage scope order, the global-beats-weak binding rule, interposition as an
ordering consequence, and the single version-matcher seam.

## Tests

`t/run.sh` builds the engine with the host compiler, builds the collision graph
with the cross toolchain, and holds the resolver to two bars.

The unit test asserts the internals a differential cannot see: the two hash
functions against their fixed points, each hash probe finding a present name and
rejecting an absent one, the global-beats-weak rule, first-definition-in-scope
order and its reversal, a local-scope definition beating a weak in the global
scope, an unresolved reference reported as not found, and the version-matcher
seam skipping a candidate a matcher rejects.

The differential is the done-when. Three objects define one symbol, `collide()`,
returning distinct tags — a deliberate three-way collision. `t/mkcollide.sh`
builds them and two PIE roots that differ only in the order they name two of
them, with the third reachable only through `LD_PRELOAD`. `t/diff-ldso.sh` asks
both loaders which object they bind `collide()` to: ours through `lookup_test`'s
`collide` mode, which walks the graph with WP-33, maps every object with WP-32,
discovers each object's dynamic view with WP-34, builds the scope, and resolves
the name; and a real glibc `ld.so`, through WSL, whose `LD_DEBUG=bindings` output
reports the object it bound the reference to — the resolution decision read
straight from the loader. Over the plain load order, the reversed load order,
and the `LD_PRELOAD` interposition, the two name the same object: `libone`,
`libtwo`, and `libthree` respectively.

## What the differential does not reach

The comparison `ld.so` is Ubuntu's, newer than el8's 2.28, the same limit
WP-33's differential carries; the resolution order this exercises is stable
across those versions, and a real vendor closure is the acceptance harness's.
The differential reads the binding from the loader's own report rather than from
the program's exit status: a first channel that ran the root and read its exit
code was dropped because a freestanding image's raw `exit` syscall does not
propagate a status under this WSL, which is a property of the environment and
not of the resolution. When no WSL `ld.so` is present the differential skips
with exit 77 rather than failing, so a host without one still builds; the
certification host has one and runs it.
