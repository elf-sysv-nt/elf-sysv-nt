# 0001 — WP-22 reopened: fault-through fails on Cygwin 3.6.10

Raised 2026-08-30. WP-22 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/core/t/run.sh` fails: the cross probe reports
`sysv_callee_saved_mask=0x0` and the fault-crossing check does not hold.

## Root cause

This is not a WP-22-specific defect. It is one face of the fault-under-a-System-
V-frame divergence recorded in `spike/abi-crossing/issue/0001`: on 3.6.10 a
fault delivered beneath a System V frame does not cross back the way it did on
3.0.7. `spike/abi-crossing`'s verdict itself flips from `yes` to `no` on this
exact case. WP-43 (signals) and WP-61 (core dumps) fail for the same reason.

## Status

WP-22 is un-delivered (removed from `doc/status/delivered.txt`) and held
(`doc/status/hold.txt`), so the autonomous worker will not attempt it. Its redo
waits on the characterization spike for 3.6.10 fault delivery, which the operator
runs in a dedicated session; that spike's answer decides whether WP-22 is a redo
or part of a redesign. The delivered source is not discarded — nothing binary is
committed — only its certification is withdrawn on the real environment.
