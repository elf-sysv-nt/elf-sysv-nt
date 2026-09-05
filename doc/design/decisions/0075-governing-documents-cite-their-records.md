# DR-0075 — the governing documents carry the citations, and a checker holds them

Status: accepted 2026-09-02
Date: 2026-09-02
Amends: AGENTS.md § Layout
Deciding: the operator, by accepting `veneer:doc/design/proposals/0006-governing-documents-and-the-citation-check.md` on 2026-09-02
Proposal: `veneer:doc/design/proposals/0006-governing-documents-and-the-citation-check.md`

## What was decided

`doc/` gains a governed set: `Requirements.md`, `Architecture.md`,
`ABI-Boundary.md`, `Verification-Plan.md`, and the three documents that
already did this job under other names, `target-definition.md`,
`licensing.md` and `AGENTS.md`. They are written by hand, they are in the
present tense, and they are current at every commit. A reader learns the
system from them without opening a record; the records stay what they have
always been, the reasoning that was live when each question closed.

Citation runs both ways. A governed section that a record settled ends in one
line naming the records, `Settled by: DR-0003, DR-0021.`, and every record
filed from this one onward carries an `Amends:` header naming the section it
changes. The reverse map is derived from the first of those, which is what
keeps the seventy-five records filed before today untouched: nothing needs a
field added to it, because reading every Settled-by line says which section
owns which record.

`bin/check-design-links` holds the pair. It asserts that every in-force record
is cited somewhere, that no citation names a record something else has
replaced, that a new record's Amends field resolves to a heading that exists,
and that the governed set is present. It runs from `ci/suites.txt` beside
`check-doc-refs`, on the same pre-commit hook.

## Why a check rather than a convention

The convention already existed. `AGENTS.md` has said since the first week that
a change updates its governing document in the same change, first or
alongside, never after; the design-gaps review of 2026-08-30 counted six
violations in ten days, and `check-doc-refs` catches only the cheapest of
them, the citation whose path no longer resolves. A rule nothing measures is
a rule about intent, and intent is not what a reader six months from now
inherits.

What the checker tests is the state of the tree, not the shape of a commit.
That distinction is the whole design. A hook cannot see whether prose and
record moved together, since it sees one commit and the convention is about
two files; it can see, at every commit, whether some governed section claims
every record that is in force. Land a record without writing the prose and
assertion 1 goes red and stays red. That is the same-change rule in the only
form something automatic can defend.

## Why the ladder stops here

Tier 1, correctness, on `doc/design/decision-ladder.md`'s reading: the six
violations were defects in the documentation of record, and one of them,
F2's `open64` bypassing the shim its base name got, was a defect in the
runtime that the missing seam document is why nobody caught. The cheapest
mechanism that closes it is a state assertion over files that already exist.
Nothing above tier 1 was consulted, because nothing needed to be.

## Consequences

Writing `Architecture.md`'s skeleton is the migration: assigning each of
seventy-five records a home is the same act as writing the section list, and
assertion 1 reports when the pass is complete. No record is edited, no file
is renamed, and nothing is renumbered.

A section may carry no Settled-by line. That is the ordinary case for
behaviour inherited from Cygwin that no record has touched, and both
`Architecture.md`'s preamble and `AGENTS.md`'s Layout section say so where a
reader will meet it, so that an unlined section is never read as an
unsettled one.

Supersession moves into the index's Status cell, appended as
`; superseded by NNNN` when the superseding record lands. A record superseded
on one point and standing on the rest does not take the marker: DR-0035 after
DR-0038 is the case in hand, the two are cited together, and the prose says
which reading is current.

`doc/history/elf-technical-breakdown.md` becomes what its own opening already
calls it, the founding survey, with a line at its head sending a reader to
`Architecture.md` for the design of record. Nothing in its body changes.

## Not verified

That nine sections can home every record without a miscellany. The assignment
in proposal 0006's execution brief was made by reading the records, and the
skeleton pass is what tests it; a record that fits none of the nine earns a
tenth section rather than a drawer.

That the derived reverse map stays adequate. It answers "which section owns
DR-0044" and cannot answer "which sections did DR-0044 change", which is a
question nobody has needed yet and which the forward `Amends` field will
answer for records filed from here on.

That a state assertion catches what a commit-shaped one would. A session can
still land the record and the prose in two commits on one branch, and the
trunk never sees the gap. That is deliberate. What it cannot do is land the
record and never write the prose.
