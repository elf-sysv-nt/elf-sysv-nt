# DR-XXXX — a program's own protection changes land at the granule, not the page

Status: accepted
Date: 2026-09-01
Deciding: the build worker, on a requirements audit, a defensible call the operator
may revisit
Proposal: none; taken when an audit found the loader's granule constraint recorded
but the same constraint on a program's own runtime calls unrecorded.

## What was decided

`AT_PAGESZ` on this platform reports 4 KB, the commit granularity (DR-0014), and
a program is entitled to read it. But protection changes go through the runtime's
`mprotect`, which on the pinned host separates protection only at the 64 KB
allocation granule (DR-0008): a change that does not start on a granule boundary
is refused, and one that does snaps to the whole granule. That constraint is
recorded for the loader's own segment mapping; this records that it reaches a
program's own `mmap` and `mprotect` calls too, and that a program must not depend
on 4 KB protection precision it can name through `AT_PAGESZ` but not obtain.

## Why it is worth recording

The loader's mapping and a program's runtime calls run through the same host
primitive, so the granule constraint is one fact with two faces, and only one was
written down. A program that reserves a region and re-protects a 4 KB page inside
it — a JIT flipping one page to executable, a GC write-barrier page, a guard page
below a stack — gets the granule behavior, not the page behavior its own
`AT_PAGESZ` advertises. The gap between the advertised page and the enforced
granule is exactly the kind of assumption that passes every unit test and fails
in a real workload, which is why it belongs in the record rather than in a reader's
memory.

## Consequences

This cannot be a build-side check; it is a runtime property of a program's own
calls. The honest enforcement is a diagnostic at the seam: the runtime's
`mprotect` already refuses a sub-granule change, and that refusal should name the
granule and this record rather than return a bare `EINVAL`, so a program that
trips it is told why. Most el8 programs never re-protect at sub-granule and are
unaffected; the ones that do — language runtimes with JITs, some GCs — meet a
documented limit rather than a silent misbehavior.

## What it does not decide

The granule value, which the runtime reads from the host at run time (DR-0008), so
a base whose `mprotect` separates at the page narrows this without a change here.
Whether a future shim could emulate 4 KB precision by tracking sub-granule intent
and re-deriving the whole-granule protection, which would be a runtime feature with
its own cost, not a requirement this records. And RELRO precision, already frozen
at the granule by DR-0008.
