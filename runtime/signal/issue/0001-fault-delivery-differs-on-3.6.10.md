# 0001 — WP-43 reopened: signal/fault delivery differs on Cygwin 3.6.10

Raised 2026-08-30. WP-43 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/signal/t/run.sh` fails: the reservation/delivery measurement
comes out differently (a negative reservation cost among them), and the run
exits non-zero.

## Root cause

The same fault-under-a-System-V-frame divergence recorded in
`spike/abi-crossing/issue/0001`. WP-43 builds the signal delivery and the
red-zone repair DR-0006 chose; both rest on how Cygwin delivers a fault beneath a
System V frame, and 3.6.10 does that differently than 3.0.7. Note that the
*reserving delivery* itself still holds the red zone on 3.6.10
(`spike/redzone-delivery/issue/0001`), so the break is in the crossing, not in
the reservation — which is a useful narrowing for the redo.

## Status

WP-43 is un-delivered and held; the worker will not attempt it. Its redo waits on
the operator's 3.6.10 fault-delivery characterization spike, which decides redo
versus redesign and can reach DR-0006. Source is retained; only the certification
is withdrawn on the real environment.
