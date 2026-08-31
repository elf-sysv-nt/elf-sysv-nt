# DR-0037 — the build and certification environment is the primary Cygwin root (3.6.10)

Status: accepted
Date: 2026-08-30
Deciding: the operator, on the machine's history and the audit below
Proposal: none; taken when a build failed on the rhel root and the audit that
followed showed the environment had never been the rhel root

Supersedes: DR-0035, on the point of which root the gate certifies against.

## What was decided

The project builds and certifies in the primary Cygwin root, `C:\-\cygwin\root`
(Cygwin 3.6.10, gcc 14.4, python 3.12, make 4.4). The rhel root,
`C:\-\rhel\root` (Cygwin 3.0.7, gcc 7.4), is retired from the build and
certification role. It was only ever the RHEL-8.10 *emulation* named in DR-0007,
and its gcc 7.4 is too old to build the 3.6.10 runtime (winsup) or to compile
the modern certifications at all; the project moved to the primary root some time
ago, and the autonomous worker had been wrongly pinned back to the rhel root by
its own `SKILL.md`.

This is consistent with, not a change to, DR-0007: the runtime base was already
Cygwin 3.6.10. What this record fixes is that the *shell, build, and
certification* environment is the primary root too, not just the source lineage.

The cross toolchain moved from `/c/-/rhel/root/home/phdye/x-elfsysvnt` to the
root-neutral `/c/-/x-elfsysvnt` (`C:\-\x-elfsysvnt`), which resolves the same
from either root; the `x-` names a cross toolchain, not an experiment. A
`~/x-elfsysvnt` symlink keeps the tests that hardcode `$HOME/x-elfsysvnt`
resolving.

DR-0035's CI gate, hard-pinned to `uname -r` = 3.0.7, is superseded on that
point: the gate certifies on the primary root now. Retiring the 3.0.7 pin and
reconciling `doc/test-environment.md` are follow-on work this record authorizes.

## Why it is not a clean switch: the audit

Moving to the correct environment surfaced what the wrong one had hidden. The
certifications and spikes had been measured in the rhel root (3.0.7), and 3.0.7
and 3.6.10 disagree on real host behaviour. Re-running everything in the primary
root:

- Nineteen WP certifications: fourteen pass, four fail — `loader/map` (WP-32),
  `runtime/core` (WP-22), `runtime/signal` (WP-43), `runtime/coredump` (WP-61).
- Eleven spikes: four hold (1, 6, 7, 8), two diverge (2, 3), five need inputs
  not on the machine (4, 5, 9, 10, 11). Each spike carries a re-verification
  report under `spike/<name>/issue/`.

Two divergences, and they are one root cause seen twice:

1. **Fault under a System V frame.** `spike/abi-crossing` (spike 3) flips from
   `yes` to `no`: its `fault-through` case goes pass to fail, everything else
   still passing. This is the gate that justified the whole architecture over
   the veneer-thunk fallback. The same fault delivery is what WP-22, WP-43 and
   WP-61 certify, and they fail for the same reason: **Cygwin 3.6.10 delivers a
   fault beneath a System V frame differently than 3.0.7 did**, and the
   fault-crossing was only ever validated against 3.0.7.

2. **Mapping over an occupied span.** `spike/map-and-jump` (spike 2) fails one
   placement case: 3.6.10 allows a mapping over an already-reserved span that
   3.0.7 refused. WP-32 relied on the refusal.

## What this record does and does not settle

It settles the environment. It does not settle whether the fault-crossing is
repairable on 3.6.10 or a wall — that is a measurement, cut as a characterization
spike, and it is the operator's to run because the answer can reach DR-0006 and
the signal design and decides whether WP-22/43/61 are a redo or a redesign.
WP-32, WP-22, WP-43 and WP-61 are reopened and held pending their spikes; the
worker will not attempt them until the holds lift. Nothing delivered by the
cross toolchain needs rebuilding — no binaries are committed and the cross
compiler's output is host-root-independent; what was invalid was the
certification, not the artifacts.
