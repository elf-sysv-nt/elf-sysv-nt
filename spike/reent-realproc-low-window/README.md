# the fixed low window mapped by a real process of the faced runtime

WP-56's `reent-tls-bringup` rung, item 1 (faced-runtime hosting). The branch
decision "the acceptance crossing hosts the faced runtime as its own process"
resolved that the crossing does not hand a low window from a parent to a
suspended cygwin child — `spike/reent-stub-realproc-window-reconcile` measured
that handover refused, blocked by the child's own reserved+committed low
occupant — but instead is itself a real process of `elfsysv1.dll`, brought up
through the WP-26 `crt0` `_dll_crt0` protocol, that lays its own address space
and maps the ELF image through its own `mmap`. That decision named one measured
question as the fork the resolved shape turns on: whether such a process, on its
`_dll_crt0`-brought-up main thread, can place a `MAP_FIXED` region at the fixed
ELF window base (`0x400000`) through its own `mmap`, where the foreign-child
handover was refused. This spike drives that question.

## What it does

`lowwindow-probe.c` is built in fault.c's shape — `-nostdlib` against the WP-26
`crt0.o` and `-lcygwin`, so `_dll_crt0` brings the faced runtime up as the
process's sole Cygwin runtime, its cygheap and reent in place before `main`.
Every faced call crosses by `sysv_abi` pointer straight at an `elfsysv1.dll`
export. On the main thread it records four layered findings:

  - `realproc_mmap_anon` — a plain anonymous `mmap` (runtime picks the address)
    returns a page it then touches: the faced `mmap` works on this thread at
    all, the bare-native-thread `cygtls` crash not applying to a `_dll_crt0`
    main thread;
  - `realproc_low_window_occupant` — a `VirtualQuery` walk of
    `[0x400000, 0x600000)` classifies what the faced runtime itself laid in the
    low window at its own startup, directly comparable to the reconcile spike's
    cygwin-child walk;
  - `realproc_mmap_fixed_free` — a `MAP_FIXED` `mmap` at a known-free low
    address (`0x10000000`) succeeds, so `MAP_FIXED` itself works through the
    faced `mmap`;
  - `realproc_mmap_fixed_window` — a `MAP_FIXED` `mmap` at `ELF_WINDOW_BASE`
    (`0x400000`) for bzip2's span, the fixed low region the DR-0008 image
    mapping needs.

`measure.sh` builds the real-process probe, runs it detached via `cmd` (the
faced runtime's console wedges on a host pty), and prints the findings as
`key=word`. Addresses and region sizes are volatile and print as context the t3
runner reduces to `0xN`; the reproducible findings are the words.

## The finding (results-2026-09-01.txt, reproduces)

`verdict=cleared`. A real process of the faced runtime maps the fixed low ELF
window through its own `mmap`, where the parent-to-foreign-child handover was
refused.

The contrast with `reent-stub-realproc-window-reconcile` is the whole point.
There the low window in a *suspended cygwin-linked child created by a foreign
parent* was `reserved+committed` — a private reservation and committed pages the
runtime had already laid over `0x400000..0x600000` before any user code ran — so
both the raw handover and `elf_window_reserve_in`'s reconcile were refused. Here,
in a *real process of the faced runtime that is the sole runtime*, the same span
reads `realproc_low_window_occupant=free` at the point `main` runs, and the
faced `mmap` places `MAP_FIXED` at `0x400000` (`realproc_mmap_fixed_window=ok`).
The low occupant the reconcile fought was an artifact of the foreign-parent /
suspended-child arrangement, not of a cygwin runtime as such; the sole-runtime
process does not carry it.

## Where this points

The resolved shape's turning question is answered yes: the crossing host, being
a real process of the faced runtime, can map bzip2's image at its fixed base
through the runtime's own `mmap`, so the parent window handover (DR-0028) and its
cygwin-child reconcile (DR-0068/DR-0069) are not needed on this path, as the
decision set out. The finishing code step the rung still owes — moving
`accept.sh`'s `build_loader` crossing onto a host that is a real process of
`elfsysv1.dll` and running bzip2's image inside it — now rests on a measured
constraint rather than an inferred one. This spike fixes and guards that
constraint; it does not itself move the run stage's halt, which advances only
when that code step lands.

## Reproducing

`./measure.sh` from this directory, or `test/t3-regen.sh`. It SKIPs
(`verdict=yes`) when the faced DLL (`a/build/wp27-face/elfsysv1.dll`) or the
WP-26 build tree (`a/build/wp26/.../crt0.o`, `libcygwin.a`) is absent, those
being uncommitted build products. The reproducible findings are the words —
`realproc_mmap_anon`, `realproc_low_window_occupant`, `realproc_mmap_fixed_free`,
`realproc_mmap_fixed_window`, and the `verdict`; addresses and region sizes ride
along as context and are reduced to `0xN` by the runner.
