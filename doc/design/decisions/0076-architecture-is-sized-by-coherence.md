# DR-0076 — the architecture document is sized by coherence, not by a word ceiling

Status: accepted 2026-09-02
Date: 2026-09-02
Amends: AGENTS.md § Layout
Deciding: the operator, in conversation on 2026-09-02, lifting a resolved
question of
`veneer:doc/design/proposals/0006-governing-documents-and-the-citation-check.md`
Proposal: `veneer:doc/design/proposals/0006-governing-documents-and-the-citation-check.md`

## What was decided

`doc/design/Architecture.md` has no word ceiling. Proposal 0006 resolved one at
6,000 words of prose, and its verification criterion 5 measured against that
number; both are retired. The operator's instruction was that the document
"must be a coherent whole document that one can implement from directly", and
that length is not the constraint that produces one.

The bar that replaces the ceiling is the one `AGENTS.md` § Layout now states: a
reader implementing a layer never has to open a decision record to find a
value, a constant, an address, a limit or a rule. A section states its numbers
outright, and where it names a measurement it names the figure.

A sub-document is created for a subsystem of significant complexity, and length
is not what qualifies one. Incoherence is: a subject is split out when a reader
of it is not reading the architecture at the time. Four exist —
`veneer:doc/design/ABI-Boundary.md`, `veneer:doc/design/Symbol-Resolution.md`,
`doc/design/Address-Space.md` and `veneer:doc/design/Runtime-Crossing.md` — plus
`doc/design/target-definition.md`, which predates this and joined the governed
set unchanged. A section that hands off keeps its own values and invariants
inline, so the architecture still reads end to end.

## Why the ceiling had to go rather than bend

The ceiling and the bar pulled in opposite directions, and the first draft
written under the ceiling is the evidence. Its sections read as a table of
contents: they named what each subsystem was for and none of the values it
turns on. A reader could not have implemented the low-window protocol, the DTV
shape or the granule refusal from any of them.

Nine sections at implementation depth do not fit in 6,000 words, and thinning
them to fit produces exactly the document the proposal was written to replace —
prose that has to be read alongside seventy-five records because it carries
none of their content. Splitting every heavy section out instead would have
kept the number and lost the whole, leaving an index rather than an
architecture.

So the ceiling was the wrong instrument. It was a proxy for readable-in-a-
sitting, and the thing it was proxying for is served by structure, not by
length: a section list a reader can hold, values stated where they are needed,
and a sub-document only where a subject is genuinely its own.

## Why this passes the ladder

It does not go through the ladder. The ceiling was a resolved question of an
accepted proposal, and the addendum to that proposal binds the executing
session not to reopen one. The executing session did not: it reported that the
ceiling and the depth requirement could not both be met and named the choice,
and the operator lifted the ceiling. Tier 8 is where a value belongs to the
operator, and this is one.

## Consequences

Verification criterion 5 of proposal 0006 is retired rather than failed. The
remaining seven criteria stand unchanged, including criterion 8's requirement
that no governed document narrate a change in the past tense.

`doc/design/Architecture.md` runs to roughly 7,400 words of prose across nine
sections, with four sub-documents behind it. Nothing measures it and nothing
should; a section that has grown incoherent is a finding a reader makes, not
one a word count makes.

The proposal's text is not edited. It records what was resolved on 2026-09-02
before execution began, and this record is what changed it afterwards.

## Not verified

That the document is in fact implementable from directly. Nobody has tried to
build a layer from it without opening a record, and until somebody does, the
bar is a claim rather than a measurement. The honest test is a session that
implements something from the prose alone and reports what it had to look up.

That four sub-documents is the right number. Two more sections carry enough
material to justify one — the thread pointer with its DTV shape, and the
veneer's classification — and both were kept inline on the judgement that a
reader of them is still reading the architecture. That judgement has not been
tested by anyone reading it cold.
