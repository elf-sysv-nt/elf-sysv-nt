# DR-0104 — acceptance is a count per substrate: rebuilt packages under N, shipped packages under H, reported side by side

Status: accepted
Date: 2026-09-06
Deciding: the run, under the operator's grant of 2026-09-06, on proposal 0013 § 2
Proposal: 0013
Amends: doc/design/Requirements.md § Acceptance

## What was decided

The platform is accepted by two counts, one per substrate, and neither is
summed with the other. Under substrate N: the number of packages in the
el8 set that rebuild from Rocky Linux 8.10's source RPMs against this
platform with no change to the package, run, and pass their own test
suites, with no substitution open against them. Under substrate H: the
number that install from Rocky Linux 8.10's binary RPMs as shipped, run,
and pass the same test suites unmodified.

The numbers are the operator's blanks, as they have been since
`Requirements.md` was first written, and are set after the reach is
measured: spike 51's census bounds N's count from above by naming the
packages whose shipped text a rebuild cannot carry; H's bound is the
kernel's syscall table. The kernel's own criteria (DR-0102) sit beneath
both counts, and a count means nothing until they pass.

## Why

DR-0079 and DR-0082, which framed acceptance as a symbol surface and a
package count over it, retired with the veneer (DR-0097), and nothing
since had said what counts. 0011 § 16 states the two userlands, rebuilt
and shipped, and the count follows the userland: a package that passes
under H proves nothing about the rebuild, and one that passes under N
proves nothing about the shipped binary, so one number would hide which
substrate a client can rely on. Tier 7 of the ladder, a reasonable default
given the two userlands; the shape has no correctness fork, and the
numbers are reserved to the operator as they were.

## Consequences

`Requirements.md` § Acceptance states the two counts and cites this record;
`Verification-Plan.md` § Acceptance says how each is credited. The
acceptance harness under `acceptance/` is rewritten to report both when a
package can first run end to end (phase 9).
