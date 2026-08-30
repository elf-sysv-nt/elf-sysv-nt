# DR-0019 — symbol lookup is a separate engine, and versioning enters through one seam

Status: accepted
Date: 2026-08-30
Deciding: the implementing agent, for WP-35
Proposal: none; recorded with the work it governs, as WP-33's and WP-34's
decisions were.

## Context

WP-34 relocated the object graph with the least symbol resolution a relocation
can stand on: a linear scan of the scope in load order, first definition wins,
weak remembered as a fallback. Its header said in as many words that the hashed
lookup, the scope and interposition rules, and versioning were WP-35 and WP-36.
This record settles how WP-35 supplies them, because the shape has consequences
for WP-36, WP-38, and any later consolidation of the two resolvers.

## What was decided

### A separate engine, not a rewrite of WP-34's scan

`loader/lookup/` is a new resolver rather than an edit to `elf_reloc.c`. WP-34's
scan stays where it is, and stays the bootstrap subset a relocation needs before
a full scope exists. The general engine is the one WP-36 and WP-38 build on. The
two agree on the binding rule, so keeping both is not two answers to one
question; it is a minimal resolver for the relocation bootstrap and a general
one for everything above it. A later change could point relocation at the
general engine once the scope it needs exists at relocation time, but neither
resolver depends on that to be correct, and WP-35 does not do it. Leaving WP-34's
certified code untouched also keeps its bar intact rather than reopening it to
carry WP-35's weight.

### The scope order is three stages, and interposition is a consequence of it

A reference is resolved against, in order, the global scope, then the
reference's own dependency list, then any `RTLD_GLOBAL` `dlopen` additions kept
as a trailing list. The global scope is itself ordered: the main object, then
the `LD_PRELOAD` interposers, then the breadth-first dependency closure. That
one ordering is the whole of the interposition policy. A definition reached
earlier in the search list wins, so a preloaded object placed right after the
main object shadows the same name in a regular dependency. `LD_PRELOAD` is not a
special case in the resolver; it is a position in the list, and the resolver has
no rule for it beyond order. Writing the order down as data — a list the scope
builder fills — rather than as branching in the search keeps the policy in one
readable place and keeps the search a plain walk.

### The binding rule is glibc's observable one

Across the whole search list the first global or GNU-unique definition wins
outright and ends the search. A weak definition is remembered but does not end
it, so a global reached later still overrides an earlier weak, and only when no
global exists anywhere does the first weak win. This is what a real `ld.so`
does, and it is what the differential holds the engine to. It matters that a
weak in an early object does not beat a global in a late one; the linear scan
WP-34 shipped already had this shape, and this record fixes it as the rule the
hashed engine keeps.

### Versioning enters through exactly one seam

Versioned lookup (WP-36) is not woven through the resolver. It enters through a
single optional argument, the `elf_version_matcher`: a callback that, for a
candidate symbol in an object, reports whether it satisfies the request and
whether it is the default (`@@`) binding, or that it must be skipped. WP-35
passes none, which is unversioned lookup. WP-36 passes a matcher that reads
`.gnu.version` against the referencing object's verneed. No other part of the
interface changes when it does. The seam is drawn in WP-35 rather than left for
WP-36 to cut so that the version matcher is a few hundred lines added at one
point, not a rewrite of the search.

## Why written from the specification

The hash tables and the resolution order are the generic ABI's and Drepper's
account, written afresh. glibc's resolver is LGPL and assumes a kernel this
platform does not have (DR-0000, DR-0004); it is read for behaviour and not
lifted, the discipline WP-31 and WP-34 already followed with their own
structures. This keeps the licence line DR-0004 turns on unbroken through the
resolver as through the parser.

## How it was certified

A deliberate three-way name collision: three objects define `collide()`
returning distinct tags, two PIE roots differ only in the order they name two of
them, and the third is reachable only through `LD_PRELOAD`. The object this
engine binds the reference to is compared against the object a real glibc
`ld.so` binds it to, the latter read from the loader's own `LD_DEBUG=bindings`
report through WSL. Over the plain load order, the reversed load order, and the
interposition the two name the same object. A unit test asserts the internals a
differential cannot see — both hash probes, the binding rule, scope order, and
the version-matcher seam. `loader/lookup/README.md` carries the account and the
two limits it does not reach.

## What this does not decide

The version matcher's own logic is WP-36's, behind the seam. The relocation
consolidation named above is left open, not scheduled. The `dl` surface's use of
`RTLD_LOCAL` and `RTLD_GLOBAL` scopes at run time is WP-38's; this record fixes
the search order those scopes are read in, not their construction across a
`dlopen`.

## When to reopen

If a measured divergence from a real `ld.so` appears that the three-stage order
or the binding rule cannot express — a resolution a real loader makes that this
model cannot — the reopen is a new record pointing back here, with the case that
forced it.
