# 0001 — the verdict flips to no on Cygwin 3.6.10: fault-through fails

Raised 2026-08-30, re-running `abi-crossing.sh` in the primary Cygwin root
(3.6.10, gcc 14) against the committed `results-2026-08-29.txt`, which was
measured in the rhel root (3.0.7, gcc 7.4). The project builds and runs in the
primary root; the original transcript did not.

## What changed

The overall verdict flips from `yes` to `no`, and exactly one case is
responsible:

    3.0.7 (committed)   fault-through: pass   verdict=yes   cases_failed=0
    3.6.10 (re-run)     fault-through: fail   verdict=no    cases_failed=1

Everything else the spike measures is unchanged and still passes on 3.6.10:
`sysv-face`, `ms-face`, `varargs`, `varargs-raw`, `callbacks` (all four crossing
kinds), and the `rz-quiet` red-zone control (1024 bytes below `%rsp` intact over
200000 passes). The regression is isolated to a fault delivered *beneath a
System V frame* not crossing back the way it did on 3.0.7.

## Why it matters

This is the spike that gated the architecture. `milestones.md` records that a
`no` here "would have sent the program to the veneer-thunk fallback, which is a
different program." The `yes` justified the direct re-facing path. On the
environment the project actually runs in, the through-a-signal half of that
`yes` does not hold.

The failure is not isolated to this spike. The same fault-under-a-System-V-frame
delivery is what `runtime/core` (WP-22), `runtime/signal` (WP-43), and
`runtime/coredump` (WP-61) certify, and all three fail their certifications in
the primary root for the same reason. This is one root cause seen four ways:
Cygwin 3.6.10 delivers a fault beneath a System V frame differently than 3.0.7
did, and the fault-crossing was only ever validated against 3.0.7.

## What it does not touch

The ABI crossing in the non-faulting directions still works: a callback down
into Windows, a variadic call, and an ms-abi/sysv-face round trip all pass. So
the boundary itself is not refuted — only fault delivery across it.

## What is owed

A characterization spike for 3.6.10's fault-under-System-V-frame delivery: is
the difference repairable in the hijack-and-return path, or a genuine wall? That
single answer decides whether WP-22/43/61 are a redo or a redesign, and it can
reach back to DR-0006 and the signal design, so it is the operator's call rather
than the autonomous worker's. Until it is answered, this spike's verdict is
`no` on the real environment and the `yes` in the committed transcript is
superseded.
