# DR-0031 — build status lives in a tracked ledger; the plan is spec, and the worker is driven from it

Status: accepted
Date: 2026-08-30
Deciding: the autonomous build worker and bin/build_status.py
Proposal: none; taken while repairing a false stop

## What was decided

The build worker no longer reads a hand-written queue, and status no longer
lives in the implementation plan.

Three things move together. The worker chooses its next package from the plan's
own dependency graph rather than from a curated list: `bin/build_status.py
--next` returns the first work package, in plan order, that is undelivered, not
held, and has every dependency delivered. Delivered status is a tracked ledger,
`doc/status/delivered.txt`, one id per line, to which the worker appends when a
package lands. Packages set aside from autonomous building are a second tracked
list, `doc/status/hold.txt`, undelivered but not attempted. The plan,
`doc/IMPLEMENTATION-PLAN.md`, is read only for structure — a section's `Needs`
line and its spec — and never written for status.

## Why

A curated queue drains. When the five ids it named were all delivered the
worker went idle with eleven packages still unbuilt, and the status line called
that a stall. The queue was the defect: a finite list cannot express "keep going
until the plan is done." The dependency graph in the plan already expresses it,
so the worker reads that instead and cannot run out until every package is
delivered.

Status in the plan was a second, quieter defect. The plan recorded completion in
two vocabularies, `Delivered` and `Closed`, and a reader that knew only the
first counted three finished toolchain packages as unbuilt and would have
rebuilt them. Keeping status out of the plan removes the ambiguity at its
source: the plan says what a package is and needs, the ledger says whether it is
done, and the two cannot disagree. The ledger is tracked rather than kept in the
ignored working tree so that its history is the audit trail — each delivery is a
commit — and so it survives loss of the machine.

## The hold on WP-15

WP-15, the compiler rebuilt against our own libc plus the C++ runtime, is on the
hold list. The three candidate dispositions — build it unattended, skip it, or
hold it — were run through the decision ladder in `~/ai/answers`. Correctness
eliminated skipping: its deliverable, and the proof that a C++ exception unwinds
across a shared-library boundary without crossing into the host-facing core,
would simply not exist. Reliability eliminated building it unattended: a
gcc-and-libstdc++ bootstrap runs for hours, well past a single scheduled session
and past the forty-five-minute stale-lock window, so a later run would steal the
lock and build over the first. Holding it was the one candidate left, and it is
the cheapest to undo — a line removed from `hold.txt` releases it to a
supervised run.
