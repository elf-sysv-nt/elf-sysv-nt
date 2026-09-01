# the low-window occupant in a cygwin-linked child at suspend

WP-56's `reent-tls-bringup` rung, item 1. `spike/reent-stub-realproc-faceload`
measured that the DR-0028 low-window handover — the front end's
`elf_window_reserve_in`, a `VirtualAllocEx` of the `0x400000` window
(`ELF_WINDOW_BASE`, size `0x3FC00000`) into the suspended child — succeeds for
the plain-PE stub but is refused (`win_err_refused`) for the real-process stub,
and read that as *the cygwin-linked child already holds the low region at
suspend*. That last clause was inference. This spike measures it.

## What it does

`occupant-probe.c` spawns a stub `CREATE_SUSPENDED` — the exact moment
`dispatch.c` attempts the handover, before `ResumeThread` and so before any user
code runs and before the statically-imported `cygwin1.dll` is mapped — walks the
child with `VirtualQueryEx`, and reports whether the window is `MEM_FREE`, what
non-free region first intersects it (classified by Type/State), and the handover
itself (`VirtualAllocEx`) reproduced against the live child. `measure.sh` builds
both stubs (real-process: `-nostdlib` against the WP-26 `crt0.o` and `-lcygwin`;
plain-PE: the WP-41 shape) and the probe, and runs the probe on each.

## The finding (results-2026-09-01.txt, reproduces)

The plain-PE child's low window is one `MEM_FREE` region and the handover
succeeds (`plain_reserve_in=ok`) — the control confirming the protocol and the
harness both work. The cygwin-linked child is different at suspend: `0x400000`
is already a **private, `MEM_RESERVE`** region covering the window base, so the
handover is refused with `err=487` (`ERROR_INVALID_ADDRESS` — the range is
already reserved). `occupant=private-reserved`, `occupant_span=covers-window-base`.

So the attribution holds, now measured rather than inferred: a cygwin-linked
executable carries a private reservation over the low ~2 MB at `0x400000` from
process creation — a reservation the plain-PE control does not make. It is not
the child's image (that links high, `spike/reent-stub-realproc-window`), and it
predates any user code, so it is the runtime's own low reservation, made for the
same low region the DR-0028 window claims. The window and the cygwin child's
low reservation are the same real estate, and that collision — not a link flag
or a stack-reserve size — is what item 1's last step must reconcile.

## Where this points

The collision is narrower than the whole window, and that is the measurement's
sharpest point: the child reserves only the low ~2 MB (`0x400000` up to about
`0x600000`); from `0x600000` the window is free for the remaining ~1 GB. So the
handover fails not because the window is occupied but because it *starts* on the
child's existing low reservation — a single `VirtualAllocEx` of the whole span
at `0x400000` cannot straddle it. The two claimants of that low region are the
same runtime: the window is reserved for the faced runtime the child will host,
and the ~2 MB already present is that runtime's own, made before any user code.
So reconciling them is identification, not eviction — the handover must
recognize the child's low reservation rather than reserve over it: adopt it
(`elf_window_adopt` accepts a `MEM_RESERVE` region at the base, though it wants
the region to span the request, which this one does not — so the window's size
or the adopt's span check is the knob), or reserve the window above the ~2 MB
the child holds. Which of those the loader wants is item 1's next design step;
this spike fixes the constraint it must satisfy. It is recorded against the rung
in `acceptance/reent/README.md`, and this spike is the guard that the collision
still reproduces.

## Reproducing

`./measure.sh` from this directory, or `test/t3-regen.sh`. It SKIPs
(`verdict=yes`) when the WP-26 build tree (`crt0.o`, `libcygwin.a`) is absent,
those being uncommitted build products. Addresses and region sizes are volatile
and ride along as context; the reproducible findings are the classification
words — `window_free`, `occupant`, `reserve_in`.
