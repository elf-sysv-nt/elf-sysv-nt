# DR-XXXX — an alias is never classified less strictly than its target

Status: accepted
Date: 2026-08-31
Deciding: WP-52 F2 redo, a defensible call under the non-reserved decision
policy in AGENTS.md; the operator may ratify or reopen
Proposal: none; taken when the redo needed its invariant stated

## What was decided

The resolution classification carries an invariant: no alias may land in a
softer bucket than its ultimate target. A forward-alias whose target needs a
shim is a shim through the same translation; a forward-alias of a stub is a
stub. `classify.py` enforces this in a second pass that runs the name-rule
output to a fixed point, and `t/reproduce.sh` re-derives the check from the
committed classification alone, so a hand-edited table cannot pass it.

## Why

The first delivery classified `open` as a shim — Linux and Cygwin disagree on
`O_*` flag values, so every call must translate its flags — while filing
`open64`, `__open64` and `__open` as bare forwards to the very same runtime
export. That is one function under four names, with the translation applied
under one of them. A caller who reached `open64` would have handed Linux flag
bits straight to Cygwin; the bug would depend on which alias the binary's
relocations happened to name, which is the least debuggable failure the veneer
could manufacture.

An alias adds no semantics. Whatever divergence the target has, the alias has,
because they are the same code behind different names. So the classification
must say so mechanically, not symbol by curated symbol: the review queue
records where semantics diverge, and the invariant propagates that finding to
every name that reaches the same place.

When the pass first ran it moved 30 rows from bucket 1 to bucket 3 — the
`open`/`openat` family and the other aliases of queued shims. None needed a
written exemption; the WP-52 spec allows one for an alias with a reason it
needs no translation, and the `O_*` table planned for WP-55 can later settle
the flag cases mechanically. Bucket 4 was untouched, since a forward's target
exists on the runtime surface by construction.

## What it does not decide

The shim designs, which remain WP-53's work, and whether any raised alias can
be argued back down to a forward. Arguing one down takes a written reason on
the record, not an edit to the generator.

## What it costs to reverse

Cheap. The pass is one function with one call site, the test check is one awk
stanza, and the classification regenerates in seconds. Reversal is a new
record pointing back here.
