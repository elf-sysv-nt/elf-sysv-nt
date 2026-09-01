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

## What the spike measures (0.3): the link target, then the live run

The terminal witness is a RUN — the veneer's own runtime-resolving thunk,
entered through the loader crossing (`enter.S`), resolving the face export from
`AT_BASE` and returning the reent-consuming result. 0.3 writes that run
(`live-run.sh`) and measures how far it gets; it does not yet pass, so
`measure.sh` still reports `verdict=staged`, but the obstacle is now measured,
not assumed.

The link-target half (carried from 0.2) records that the veneer→face crossing
*target* is real and matched end to end — the precondition the live run rests
on. With all three prerequisites present, `measure.sh` builds the veneer and
records:

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

## The live run (0.3), and where it halts

`live-run.sh` builds the loader (the WP-41 stub with the DR-0058 crossing), the
WP-53 veneer, and a reent-consuming forward specimen (`reent-spec.S` +
`reent-body.c`, dyn-cross-spec.S's shape carried to the reent), then takes the
specimen through the loader three ways and records what each reaches:

  - `veneer_maps_as_elf_runtime=yes` — the veneer maps as an `--elf-runtime`.
    This needed a fix: `veneer/libc/build-libc` linked with `ld` directly and so
    missed the cross gcc's `max-page-size=0x10000` default, leaving two PT_LOAD
    segments of unlike protection on one 64K granule; the loader correctly
    refused it (`elf_map_err_granule`, DR-0008). The build now sets it, and the
    veneer maps. This unblocks the bzip2 run stage too, which maps the same
    veneer.
  - `crossing_enters=yes` — with the veneer mapped, the specimen enters through
    `enter.S` and its `strtol` PLT call reaches the veneer's own thunk, which
    runs and null-faults *only because no face base was supplied* — resolver.c's
    honest failure for an unresolved export, not a crossing failure.
  - `face_base_via_runtime=no` — the one step not yet wired. `--runtime`
    `LoadLibraryA`s the faced `elfsysv1.dll` so its base reaches the veneer's
    resolver through `AT_BASE`; from the Cygwin stub this is the cygload shape
    `reent-bringup` found wedges, and it does — `error 1114`,
    `heap allocated at wrong address` (DR-0060). So `AT_BASE` carries no face
    base, the thunk cannot resolve `strtol`, and `reent_live_run=faulted`.

The measured picture: the ELF crossing is no longer the obstacle — it enters and
runs the reent-consuming forward thunk. What stands between here and green is the
real-process face-load (item 1's remaining half): bringing the faced runtime up
so its base reaches the image, without the `LoadLibraryA` heap-reservation
wedge. The `errno` read-back is a further step behind the errno value-translation
shim (DR-0000), which the veneer does not emit — `reent-body.c` measures the
forward `strtol` return as the reachable witness and records why.

## Why it is not yet registered

It is deliberately NOT in `test/spike-regen.tsv`: the live run is now written and
measured, but it does not yet pass — it halts at the face-load
(`reent_live_run=faulted`), so this is a staged characterization of where the
crossing stops, not a certified reent run. A staged spike registered with the
runner would pin a fault as if it were the answer. Until the veneer thunk
resolves and returns the reent across the loader crossing (`verdict=pass`),
`to-green`'s `reent-tls-bringup` row stays `-`. The spike is registered, and its
transcript pinned, once that run passes.

## The subtlety this spike is where to measure, not assume

`enter.S` parks the host stack and restores it around the crossing, because
Cygwin finds `_my_tls` from the stack pointer and the ELF world runs on its own
low-window stack. A reent-reading-and-writing body reached across that boundary
is the first exercise of that mechanism for the reent itself, so this spike is
where the ELF-frame reent shape is measured rather than asserted — the "runtime
face at a DLL's width" line the plan's `Not verified` section keeps open.
