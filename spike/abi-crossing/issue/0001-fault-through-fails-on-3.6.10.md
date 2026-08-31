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
kinds), and the `rz-quiet` red-zone control (1024 bytes below `%rsp` intact ove
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
reach back to DR-0006 and the signal design, so it is the operator's call rathe
than the autonomous worker's. Until it is answered, this spike's verdict is
`no` on the real environment and the `yes` in the committed transcript is
superseded.

## Characterized, 2026-08-31

The measurement was taken and it does not support this report's root cause.
`characterize-fault-through.sh` builds `fault-probe.c` at three optimization
levels and runs each case in its own process; the transcripts are
`results-fault-through-3.6.10-2026-08-31.txt` and
`results-fault-through-3.0.7-2026-08-31.txt`, and the spike README's "The
fault-through characterization" section carries the full reading. In short:

- **The two hosts agree on the crossing.** A fault in Microsoft code beneath a
  non-leaf System V frame recovers on 3.6.10 exactly as on 3.0.7, in all six
  shapes measured and at every optimization level. 3.6.10 does not deliver a
  fault beneath a System V frame differently.
- **What changed is the compiler.** The spike raises its fault with
  `*(volatile int *)0 = 1`, which is undefined behaviour, and gcc 14 concludes
  the path is unreachable and removes the *call site* that reaches it. At `-O1`
  and `-O2` the binary contains no call to the System V faulter at all. gcc 7.4
  keeps it. So the case reports that the fault did not come back because the
  fault does not happen.
- **The case cannot tell that from a delivery failure.** "The fault never came
  back as a signal" is what it prints for a fault that was never raised and fo
  a fault that was raised and lost. That is why the probe answers by exit
  status, with `nofault` and `lost` as separate outcomes.
- **DR-0012's tripwire has not tripped.** Re-measured under gcc 14 through
  `RtlLookupFunctionEntry`: an `ms_abi` non-leaf carries a `RUNTIME_FUNCTION`
  and a `sysv_abi` non-leaf carries none, as that record requires.

What is owed is a specimen the compiler cannot reason away -- `runtime/signal`'s
suite raises its fault with `ud2` in assembly and never had the problem. The
repair is not this measurement's to make, and neither is the verdict on whethe
this spike's `no` is withdrawn; both are the operator's. The measurement
obligation is discharged.
