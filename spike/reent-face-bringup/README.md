# reent-face-bringup — item 3 of the reent-tls-bringup rung

`acceptance/reent/README.md` orders the `reent-tls-bringup` road-to-green rung
into three items. Items 1 and 2 have closed their reachable-on-the-host phases:
the loader stub is relinked in the real-process shape (`loader/exec/realproc/`,
routing option parsing and `--version` through the seam without regressing the
WP-41 exec-* bar), and `veneer/libc/generate.py` emits each forwarding FUNC as a
runtime-resolving thunk against the WP-27 face. What both still owe is the run:
a reent-consuming body reached through the veneer thunk, into a real
`elfsysv1.dll` face, returning its result in the reent the caller reads.

This spike is that run — item 3. It asks one question:

    Does a reent-consuming libc body, reached through the WP-53 `libc.so.6`
    veneer resolving into a built `elfsysv1.dll` face, set the caller's reent?

The witness is `spike/reent-bringup`'s: `strtol` on an overflow returns
`LONG_MAX` and sets `errno` to `ERANGE` in the reent `__errno` hands back —
measured this time across the veneer→face resolution rather than in a
hand-built probe.

## What it depends on, and why it is not yet registered

The measurement needs three built artifacts that are scratch, not committed, and
none of which stands on the host toolchain alone:

  1. the WP-26 winsup DLL (`runtime/winsup/build.sh`) — `elfsysv1.dll` with the
     reent brought up the sanctioned way, under `a/build/wp26`;
  2. the WP-27 face on it (`runtime/face/build.sh`) — the System V export
     surface the veneer thunk resolves into, under `a/build/wp27-face`;
  3. the WP-53 `libc.so.6` veneer (`veneer/libc/build-libc`) — the ET_DYN whose
     runtime-resolving thunks name the face exports.

Until those three build on this tree, the question cannot be measured, only
staged. So `measure.sh` here is a WIP skeleton: it names the three artifacts,
builds what it can, and reports which prerequisite is absent rather than
asserting a finding. It is deliberately NOT in `test/spike-regen.tsv` yet — an
unrun spike registered with the runner is an INCOMPLETE certification, not a
pass. It is registered, and its transcript recorded, only once the three
artifacts build and the crossing runs.

## The subtlety this spike is where to measure, not assume

`enter.S` parks the host stack and restores it around the crossing, because
Cygwin finds `_my_tls` from the stack pointer and the ELF world runs on its own
low-window stack. A reent-reading-and-writing body reached across that boundary
is the first exercise of that mechanism for the reent itself, so this spike is
where the ELF-frame reent shape is measured rather than asserted — the "runtime
face at a DLL's width" line the plan's `Not verified` section keeps open.
