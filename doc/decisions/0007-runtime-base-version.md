# DR-0007 — the runtime is based on Cygwin 3.6.10, not the pinned 3.0.7

Status: accepted
Date: 2026-08-30
Deciding: the operator
Proposal: none; taken when WP-20 needed a named ref for the export surface

## What was decided

`elfsysv1.dll` is built from Cygwin 3.6.10, the `newlib-cygwin` tree at commit
`b11613e47` in `C:\-\cygwin\root`'s development lineage. Every artifact that
reads Cygwin's source for the runtime's own shape — the export inventory first,
the down-call wrappers and the veneer version map after it — takes it from that
ref.

The 3.0.7 in the rhel root is not the runtime base. It is the RHEL-8.10
emulation and verification target, pinned so behavior can be checked against
what el8 actually ships. The two versions have different jobs, and this record
keeps them from being conflated: 3.6.10 is what the runtime is *made of*, 3.0.7
is what the result is *checked against*.

## Why 3.6.10

The runtime is built where the project builds, which is the primary root with
its modern toolchain, and the `newlib-cygwin` checkout there is already at
3.6.10. Sourcing the runtime from the same place it is compiled is the ordinary
arrangement; sourcing it from the frozen 2019 verification root would mean
building the thing this project ships out of a tree kept deliberately old for a
different purpose.

The export surface makes the point concrete. WP-20 cuts `elfsysv1.dll`'s outward
list from `cygwin.din`, and that list is what WP-21 wraps and WP-51 maps. It has
to be one version's list, named, because the two versions do not carry the same
exports; 3.6.10's is what a program built against this runtime will find, so it
is the one the veneer must reproduce.

## What it does not decide

The compatibility counter. `elfsysv1.dll` inherits Cygwin's backward-only rule
and carries its own API major and minor, WP-25's work; this record fixes what
release the surface is cut from, not how that surface is versioned afterwards.

The verification floor. Checking a built package against el8 still happens
against 3.0.7 and the real RHEL host, unchanged. Naming 3.6.10 as the base does
not move the target the acceptance comparison runs against.

## What it costs to reverse

Cheap now, dearer later, which is why it is worth naming before WP-21 begins.
Today reversal is one regeneration: `extract-exports.sh` against a different
`--din` produces a different inventory and nothing else has been built on the
first. Once WP-21's wrappers and WP-51's map exist, they are shaped by this
surface, and a change of base re-cuts all three together.

Reversal is a new record pointing back here, not an edit to this one.

## Where it is written down

`runtime/exports/README.md`, which names the ref and whose reproduce test pins
it. `doc/IMPLEMENTATION-PLAN.md`, WP-20, where "a named ref" becomes this ref.
`doc/test-environment.md`, which already separates the two roots by their jobs
and can now cite this record for which one the runtime comes from.
