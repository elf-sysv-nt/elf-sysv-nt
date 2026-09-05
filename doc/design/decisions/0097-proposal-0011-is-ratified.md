# DR-0097 — proposal 0011 is ratified, and the veneer's records retire with it

Status: accepted
Date: 2026-09-05
Deciding: the operator, by accepting proposal 0011
Proposal: 0011
Amends: doc/design/Architecture.md § What this document covers

## What was decided

Proposal 0011 is ratified. The boundary this platform emulates Linux at is the
`syscall` boundary, not the C library's, and the design in 0011 is the design
of record for everything above the substrate line.

Every decision record whose subject is the veneer arc retires with it. 0011's
cross-cutting section names most of them and says they are "retired together by
the ratifying record, not amended"; this is that record, and it was owed from
the moment 0011 was accepted. Those records are not edited and not deleted.
They stand where they were taken, describing a bootstrap the new design does
not pass through, and their Status cells in the index say so.

Eleven records survived the repository split and retire here:

| record | what it settled | why it retires |
|---|---|---|
| DR-0007 | the runtime is based on Cygwin 3.6.10 | there is no Cygwin runtime |
| DR-0009 | the down-call wrapper is an ms_abi tail jump | there is no down-call |
| DR-0010 | the veneer's `features.h` is el8's arithmetic, copied | there is no veneer header |
| DR-0012 | host-facing entry points are ms_abi with unwind data | there is no host-facing seam |
| DR-0015 | the variadic veneer rebuilds a Microsoft `va_list` | there is no variadic veneer |
| DR-0018 | the compatibility counter is Cygwin's, re-faced | there is nothing re-faced |
| DR-0020 | callback trampolines are per-shape compiled thunks | nothing crosses a shape boundary |
| DR-0025 | initialization order across the ABI boundary | there is no ABI boundary |
| DR-0034 | the installer's only memory is a manifest | there is no installer |
| DR-0040 | the faced DLL is installed by rename | there is no faced DLL |
| DR-0059 | the loader runs `DT_INIT` across the ABI boundary | the boundary the rule crossed is gone |

## Why

The test is not whether a record was useful, and not whether the reasoning in
it was sound. It is whether a governing document in this tree can state, in the
present tense, a thing the record settles. For each of the eleven the answer is
no, and not because the work is unfinished: the subject is absent. A record
about installing a DLL that this repository does not build is not a pending
obligation, it is a description of somewhere else.

Settled by the ladder at tier 1. Correctness discriminates outright here, which
is unusual for a curation question and is the reason this one did not have to
go to the operator as a matter of taste. Where it did not discriminate the
record stays: DR-0011's cache format, DR-0016's relocation certification,
DR-0027's exec branch, DR-0029's fork, DR-0033's core file and DR-0073's weak
undefined all describe things the new design still has, in different clothes,
and none of them is retired by this record.

## Consequences

`bin/check-design-links` reads `; superseded by 0097` from the index Status
cell and stops requiring a Settled-by line for those eleven, which is what lets
`Architecture.md` be rewritten for a system that has no ABI seam in it. The
records themselves remain readable at their own paths.

DR-0000, and the fifty-odd records 0011 names by number, were retired by the
same reasoning one step earlier: they did not carry into this repository at
all, and `doc/history/the-veneer-arc.md` says where to read them.

Nothing here reverses a record. A retired record is one whose subject left, not
one whose argument was wrong, and the difference matters to anyone reading the
veneer sibling to find out why that arc went the way it did.
