# DR-0078 — doc/ splits by what a file claims about the present

Status: accepted 2026-09-03
Date: 2026-09-03
Amends: AGENTS.md § Layout
Deciding: the operator, by instruction on 2026-09-03
Proposal: none; the operator specified the split and the order it is applied in

## What was decided

`doc/` is three shelves, and a file's shelf is decided by what it claims about
the present rather than by its subject.

`doc/design/` carries the governed set, its four sub-documents, the decision
records, the proposals behind them, and the smaller documents that state a
rule: the ladder, the licence reading, the stub definition, the veneer's own
inventory of what it lacks, the substitutions ledger, the test environments.
Everything on that shelf reads as current.

`doc/history/` carries what was true. The three surveys of 2026-08-20, the
closed design-gaps review, and the landed handoff sit there, and nothing on
that shelf is authority for present behaviour.

`doc/` itself keeps the plans and the ledgers, which claim neither: the
implementation plan, the roadmap, the milestones, the deferred-work register,
`doc/status/`, and the generated dashboard. A plan says what is intended, and
intent is not state.

## Why a shelf rather than a naming convention

The tree already had the distinction and carried it in prose. `Architecture.md`
says at its head that the founding survey is history; the survey says the same
thing from its own side. Both sentences are correct, both were written by
somebody who had just been confused, and neither survives a reader who arrives
at `doc/` and sees twenty-eight files in one list. A directory answers the
question before it is asked, which a sentence three paragraphs into a document
cannot.

The failure this prevents is specific, and it has already happened here: an
observation from `elf-technical-breakdown.md` — glibc's resolver is GPL — was
promoted to a live constraint and eliminated the right answer for a session,
because the survey read as current. It was never current. It was a recollection
from the week the project opened, and the document that carries it says so.

## Why the plans stay outside both shelves

A plan is a third kind of claim, and folding it into either shelf loses
something. Filed under `design/`, `IMPLEMENTATION-PLAN.md` would read as a
statement of what the system is, which it is not and says it is not.
Filed under `history/`, it would read as retired while it is still the thing
the build worker consumes. Scope and progress rot at different rates, which is
the reason `ROADMAP.md` already refuses to carry status; the same argument puts
the plans on their own shelf.

## Why the ladder stops at tier 5

Correctness, reliability and robustness are all indifferent here: no
arrangement of directories makes a document say something false. Diagnosability
discriminates, and it does so on the case that costs a reader something —
following a citation into a document that turns out to be superseded reasoning,
and having nothing in the path to warn them.
`doc/history/elf-userspace-execution.md` warns before the file opens; the same
document one directory up did not. Tiers 6 and 7 were not consulted.

## Consequences

Sixty-five citations of the old decisions directory, and about four hundred of
the moved documents, were rewritten in the same commit as the move, which is
what makes `bin/check-doc-refs` a real gate over the change rather than a
formality after it.

Six tools carried the old paths as literals and now carry the new ones:
`check-design-links`, `check-doc-refs`, `allocate-id`, `normalize-logs`,
`register-dr.py`, `refresh-status-reports.py`, with `session-land` and
`.gitattributes` beside them. `check-design-links` had one regex that accepted
any bare name directly under `doc/` as an `Amends:` target and nothing deeper;
it now requires `doc/design/`, so a record amending something off the governed
shelf fails rather than passing unexamined.

The old decisions path is gone. Anything outside this repository that
cited it — a bookmark, another checkout's notes — is broken, and there is no
redirect. That is the cost, it was paid once, and paying it later would have
cost more.

## Not verified

That `doc/history/` stays a shelf rather than becoming an attic. Nothing
checks that a document there is still worth keeping, and the mechanism that
would — a review date, a retirement rule — is not written. The three surveys
earn their place by being cited; a handoff that nothing cites is a different
case and this record does not settle it.
