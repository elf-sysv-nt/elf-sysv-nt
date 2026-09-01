# reent-stub-realproc-faceload — WP-56 reent-tls-bringup, item 1's last owed step

The `reent-tls-bringup` rung (`acceptance/reent/README.md`) has the loader stub
relinked in the real-process shape and the veneer emitting runtime-resolving
thunks. `spike/reent-stub-realproc-run` measured the real stub linking and
running that shape standalone — it reaches `--version` across the faced runtime
and crosses fd 2 for its window diagnostic. What that spike left for "the next
step this rung takes" is the `--runtime` face-load driven **through the front
end**, since the real-process stub reserves the low `0x400000` window only when
a parent front end reserves it into the suspended child (a standalone
`--self-window` reserve is refused).

This spike is that run. It composes the real-process stub with the WP-41 front
end (`elfsysv-exec`) and drives a reent-consuming ELF specimen through the
crossing, exactly as `spike/reent-face-bringup`'s live run does — but with the
**real-process** stub (`-DELFSYSV_REALPROC`, `-nostdlib` against the WP-26
`crt0.o` and `-lcygwin`) as `ELFSYSV_STUB` rather than the plain-PE cygload stub
that spike found wedges on the face base (`error 1114`).

## The question

    Driven through the front end so the parent reserves the low window into the
    suspended real-process child, does the real-process stub's `--runtime`
    `LoadLibraryA` of the faced `elfsysv1.dll` reach the face base — the half the
    plain-PE cygload stub could not — so `AT_BASE` carries it to the veneer's
    resolver?

`spike/reent-stub-faceload` measured the face-base half in miniature (a
real-process host's `LoadLibraryA` returns the runtime's base, no `1114` wedge);
this measures it on the actual stub, front-end-driven.

## What it found (measure.sh, 2026-09-01)

The stub, the front end, the veneer, and the reent-consuming specimen all build.
Driven through the front end, the plain-PE control stub receives the low-window
handover and runs past it (`plain_stub_gets_window=yes`) -- so the DR-0028
handover and this harness both work. The real-process stub does not:
`realproc_stub_gets_window=no`. The front end's `VirtualAllocEx` of the low
`0x400000` window into the suspended child is refused (`win_err_refused`),
because the real-process stub is linked against `cygwin1.dll`, which the Windows
loader maps into the suspended child and which already holds the low region at
suspend -- before any user code runs. So the terminal run halts at the window
handover, before the `--runtime` faceload (`reent_faceload_run=blocked`,
`verdict=staged`).

This relocates item 1's last owed step: not the faceload itself (its base
reachability is already clear in the sanctioned shape, `spike/reent-stub-faceload`),
but reconciling the low-window handover with a cygwin-linked child -- the child
that holds the low region is the same runtime the window is being reserved for.
That is the next rung this measures the ground for; the face-base half stays
measured in miniature until it lands.
