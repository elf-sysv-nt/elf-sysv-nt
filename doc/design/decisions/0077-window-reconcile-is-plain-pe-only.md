# DR-0077 — the window reconcile of DR-0068 and DR-0069 is live for the plain-PE shape only

Status: accepted 2026-09-03
Date: 2026-09-03
Amends: doc/design/Address-Space.md § The plain-PE shape, end to end
Deciding: the operator, on the open-items dispatch of 2026-09-03
Proposal: none; taken when a documentation pass found two accepted records
modelling a state no measurement supports

## What was decided

DR-0068 and DR-0069 are in force for the plain-PE shape: a foreign PE stub
created suspended by a parent that reserves the low window into it. They are
not in force for the sole-runtime shape, where DR-0071 sets the whole
parent-handover arrangement aside, and they do not describe what a real
cygwin-linked child presents.

Both records stay standing. Neither is superseded, because each is correct
about the shape it governs, and the index carries no supersession marker for
either.

## Why the scoping is needed

DR-0068 models the child's low window as the child's own private
`MEM_RESERVE`, and builds a reconciling fallback that identifies such a region
and reserves around it. DR-0069 extends the same model to the placement side.
Both are sound against that model.

The model is not what a real cygwin-linked child presents.
`veneer:spike/reent-stub-realproc-window-reconcile/results-2026-09-01.txt` measures
the low window as `reserved+committed`, with two committed regions above the
reservation. `elf_window_plan` refuses any committed occupant by design — a
committed region is not a bare reservation and cannot be reconciled — so
`elf_window_reserve_in` returns `win_err_refused`, and `elf_window_adopt`
refuses on the same ground. The reconcile does not clear such a child.

The occupant spike that preceded DR-0068 had already recorded the two committed
regions in its own transcript. DR-0068's context paragraph describes only the
reservation.

DR-0071 is the only record that acknowledges this, and it does so obliquely: it
sets the parent handover and its reconcile aside for the shape it governs, and
attributes the committed occupant to the foreign-parent arrangement rather than
to a Cygwin runtime as such. Neither DR-0068 nor DR-0069 carries an amendment
or a pointer to it, so a reader arriving at either record from the index finds
an accepted record describing a mechanism that does not do what it says on the
case most likely to be in front of them.

## Why a record rather than an edit

The append-only rule, and the fact that nothing here reverses either record.
Editing DR-0068's context to describe a committed occupant would rewrite the
reasoning that was live when the reconcile was designed into the reasoning that
is live now, and would leave a reader unable to see why the fallback has the
shape it has. What is wanted is a boundary around two correct records, not a
correction inside them.

## Why the ladder stops at tier 1

Correctness, and it discriminates on the first rung. A reader who builds
against DR-0068's model for a cygwin-linked child builds against a state no
transcript supports, and the failure is a refusal at bring-up rather than
anything a later tier would weigh. The cheapest mechanism that closes it is a
scoping record over files that already exist. Nothing above tier 1 was
consulted.

## Consequences

`doc/design/Address-Space.md` states the scope in prose and describes the
sole-runtime shape separately, so the governing document answers the question
without a reader reaching the records at all.

The reconcile's own capacities become worth stating, since they are neither
record's: both the reservation walk and the release walk carry
sixty-four-element region arrays, and a window fragmented past that is refused,
with the release path clearing its held flag before it fails.

Nothing changes in either record's mechanism, in `reserve.c`, or in the
plain-PE certifications.

## Not verified

That the committed occupant is a property of the foreign-parent arrangement
rather than of a Cygwin runtime. DR-0071 asserts the first reading and the
measurement does not separate them: no transcript shows a cygwin-linked child
created by an ordinary Cygwin parent, which is the control that would.

That the plain-PE shape is one this project keeps. It is what the exec suite
certifies today, and the acceptance crossing is scheduled to move to the
sole-runtime shape. If that move completes and nothing else uses the parent
handover, the scope this record draws is around a mechanism with no remaining
consumer, and retiring it is a different decision from bounding it.
