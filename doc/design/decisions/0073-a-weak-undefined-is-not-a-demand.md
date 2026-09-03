# DR-0073 — a weak undefined symbol is not a demand on the runtime

Status: provisional
Date: 2026-09-02
Deciding: the build worker, on WP-56, a defensible call the operator may revisit
Proposal: none; taken when the first acceptance run under el8's own build flags
reported two of the vendor's own symbols as unclassified

## What was decided

A symbol a binary leaves undefined and marks weak is classified `optional`
rather than `unclassified`, and an optional symbol does not stand between a
package and `ready`. Weakness excuses an absence; it does not conceal a symbol
the veneer carries, so a weak symbol that is in the classification map still
reports its own bucket — forward, wired, shim, filled — exactly as a strong one
does. Only a weak symbol the map does not carry becomes optional.

This amends DR-0057, which settled `ready` as forward, wired or filled over the
whole surface. That reading is unchanged for every symbol the program actually
requires. What changes is which symbols count as required.

## Why weakness settles it

ELF says so. A weak undefined symbol that no object defines resolves to zero,
the link succeeds, and the obligation to cope falls on the program, which
tests the address before using it. That is the mechanism's entire purpose:
a way to ask without requiring. Treating such a symbol as an unmet demand
inverts the meaning of the marking.

The measurement that forced it is more pointed than the principle. bzip2 built
under el8's effective flags reported `_ITM_registerTMCloneTable` and
`_ITM_deregisterTMCloneTable` as unclassified, which dropped its verdict from
`ready` to `needs-wiring`. Both are `NOTYPE WEAK`, unversioned, emitted by
crtstuff into every position-independent executable gcc links; they exist so a
transactional-memory runtime can register clone tables if one is present, and
in an ordinary glibc link nothing defines them. Red Hat's own shipped
`/usr/bin/bzip2` carries both, and `__gmon_start__` beside them, on the same
terms — read from the vendor rpm, not inferred.

So the harness was about to report that el8's own binaries make demands el8's
own libc does not satisfy. A rule that convicts the vendor of shipping broken
software is measuring the wrong thing, and the whole of a 2900-package corpus
was on the far side of it: every PIE in the distribution carries these.

## Consequences

`acceptance/classify.awk` takes a fourth input, the weak subset, and emits
`optional`. `accept.sh` derives that subset from `nm -D --undefined-only`'s
type letter — `w` for a weak undefined, `v` for a weak undefined object —
counts the category, prints it under its own heading, and carries
`optional:N` in the machine-readable line beside `unclassified:N`. The two
stay separate on purpose: a symbol nobody has classified and a symbol the
program can live without are different facts, and collapsing them would lose
the first to make the second read better.

bzip2 goes from `unclassified:2` to `optional:2` and its remaining blocker is
one symbol, `__fprintf_chk`.

## What it does not decide

Whether a weak symbol the veneer *does* carry should be provided. It should,
and it is: weakness changes nothing about a symbol in the map. The rule is
about absence alone.

Whether the fortified entry points are owed. `__fprintf_chk` arrives with
`-D_FORTIFY_SOURCE=2`, is strong, and is a genuine demand this record does not
soften. `veneer/README.md` records the fortify family as deferred and
`doc/deferred-work.md` carries it as an item with no owning package; it now
blocks WP-56, which is a stronger claim on it than either.

Whether `nm`'s type letters are the right source. They are what the harness
already reads for the surface itself, so this adds no new dependency, but a
reader who wanted the `st_info` binding directly would take it from
`readelf --dyn-syms`. Both say the same thing about the same symbol table.
