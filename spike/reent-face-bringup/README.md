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

## What the skeleton measures today (0.2), and what it still stages

The terminal witness is a RUN — the veneer's own runtime-resolving thunk,
entered through the loader crossing (`enter.S`), resolving the face export from
`AT_BASE` and returning the reent-consuming result. That run is not yet written,
so `measure.sh` reports `verdict=staged`.

What 0.2 adds over the file-existence check it replaced is the reachable half
measured rather than assumed. With all three prerequisites present, `measure.sh`
builds the veneer and records that the veneer→face crossing *target* is real and
matched end to end — the precondition the live run rests on:

  - `strtol_body_is_thunk=yes` — the reent-consuming body is a real
    runtime-resolving thunk (12 bytes, entry `0x4c` `lea`, not the single-byte
    `0xc3 ret` stub `reent-veneer-runtime` found), so item 2's codegen is in the
    built veneer.
  - `reent_thunk_keys_on_face_name=yes` — the literal `strtol` is in the veneer,
    the name the thunk's resolver hands the face's PE export directory at run
    time.
  - `reent_carrier_present=yes` — `errno@@GLIBC_PRIVATE`, the TLS carrier the
    body writes and the ELF caller reads, is a defined dynsym.
  - `face_exports_reent_target=yes` — the built `elfsysv1.dll` face exports
    `strtol`, so the run-time resolution has a target.
  - `veneer_face_target_matched=yes` — the name the thunk keys on *is* a real
    face export: the crossing has a target end to end.

Neither prior spike measured this pair: `reent-veneer-thunk` had no face DLL,
and `reent-veneer-runtime` predates the thunk bodies. The three artifacts the
measurement rests on are scratch build products, not committed:

  1. the WP-26 winsup DLL (`runtime/winsup/build.sh`) — `elfsysv1.dll` with the
     reent brought up the sanctioned way, under `a/build/wp26`;
  2. the WP-27 face on it (`runtime/face/build.sh`) — the System V export
     surface the veneer thunk resolves into, under `a/build/wp27-face`;
  3. the WP-53 `libc.so.6` veneer (`veneer/libc/build-libc`) — the ET_DYN whose
     runtime-resolving thunks name the face exports.

So `measure.sh` SKIPs to `verdict=staged` when the cross toolchain or the face
DLL is absent, as the sibling reent spikes do.

## Why it is not yet registered

It is deliberately NOT in `test/spike-regen.tsv`: the terminal live-run witness
is unmeasured, so this is a staged characterization of the crossing target, not
a certified run — and an unrun spike registered with the runner is an INCOMPLETE
certification, not a pass. Until the veneer thunk resolves and returns the reent
across the loader crossing, `to-green`'s `reent-tls-bringup` row stays `-`. It is
registered, and its transcript recorded, once that run runs.

## The subtlety this spike is where to measure, not assume

`enter.S` parks the host stack and restores it around the crossing, because
Cygwin finds `_my_tls` from the stack pointer and the ELF world runs on its own
low-window stack. A reent-reading-and-writing body reached across that boundary
is the first exercise of that mechanism for the reent itself, so this spike is
where the ELF-frame reent shape is measured rather than asserted — the "runtime
face at a DLL's width" line the plan's `Not verified` section keeps open.
