# DR-XXXX — the acceptance crossing hosts the faced runtime as its own process

Proposed 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
item 1 (the faced-runtime hosting the run halts on).

## Context

bzip2 clears import resolution and enters the crossing (march a92adbf and
after), but `./accept.sh bzip2` still fails: the suite's first compress
(`./bzip2 -1 < sample1.ref`) exits 5 with no output, and driving the crossing
directly on `bzip2 --help` reproduces the same exit 5 with no output. The image
enters — window held, four PT_LOAD mapped, dynamic, GOT/PLT resolved, two
initializers run — then halts on its first live libc call. The reason is
measured, not inferred: `accept.sh`'s run stage maps only the ELF veneer as
`--elf-runtime`, for GOT/PLT import resolution, and passes no faced runtime, so
`AT_BASE` is zero and every veneer forwarding thunk (`strtol`, `malloc`,
`printf`, …) resolves against a null base and faults. The obstacle is that the
crossing hosts no faced runtime whose exports the thunks can reach.

DR-0060 named the reent-bearing host shape as the real process of the faced
runtime rather than a cygload call, and DR-0066/DR-0067 relinked the loader's
host stub into a real-process shape that does its own work host-safe. But that
line kept two runtimes in the picture: the host stub is a Cygwin process (it
holds `cygwin1.dll` for the DR-0008 mapping mmap), and the faced `elfsysv1.dll`
was to be brought in beside it. Every measurement of that arrangement failed
the same way, and the failures agree on one cause.

## Evidence

Three measurements, each reproducing:

The cygload faceload wedges. Adding `--runtime=<host>/elfsysv1.dll` to the
Cygwin-hosted stub — its existing `AT_BASE` LoadLibrary path — aborts in the
faced runtime's cygheap bring-up: `heap allocated at wrong address 0x0 (mapped)
!= 0xA00000000 (expected)`, `error 1114` (`spike/reent-face-bringup`,
`spike/reent-stub-realproc-faceload`). A second Cygwin runtime in a process that
already has one collides.

The parent window handover is refused by the child's own low region. The front
end reserves the low window (`0x400000`, `0x3fc00000`) into a suspended child by
`VirtualAllocEx` (DR-0028). Against a plain-PE child this holds; against a
cygwin-linked real-process child it is refused, because the child already holds
a private reservation and committed pages over the low span before any user code
runs (`spike/reent-stub-realproc-window-occupant`,
`spike/reent-stub-realproc-window-reconcile`:
`low_window_occupant=reserved+committed`). The DR-0068/DR-0069 reconcile
(`elf_window_reserve_in`'s walk-and-plan fallback, and `elf_window_adopt`)
refuses any committed occupant by design, so both the reserve and the adopt
verb return `win_err_refused` against a real child.

A faced service called from a bare native thread crashes. A native process
loads the faced runtime cleanly (`runtime/face/t/hostload.sh` verdict yes,
DllMain and the PE TLS callback fire), but calling the faced `mmap` from a bare
native thread segfaults: the thread has no `cygtls` (the A′ measurement,
`a/build-blockers.log`).

The three are one finding. A faced service (its `mmap`, its reent-consuming
libc bodies) runs only on a thread the faced runtime's own `_dll_crt0` startup
brought up, and a process holds exactly one Cygwin runtime. A host-Cygwin stub
that also loads the faced runtime is two runtimes and collides; a native host
that loads it has no `_dll_crt0`-initialized thread to call it from.

## Decision

The acceptance crossing hosts the faced runtime as its own process: the crossing
process is a real process of `elfsysv1.dll`, brought up through the WP-26 `crt0`
`_dll_crt0` protocol so the faced runtime is the process's sole Cygwin runtime,
with the cygheap at its address and per-thread reent set up before any image
code runs. The DR-0008 image mapping goes through that faced runtime's own
`mmap`, so its `fork` replays a mapping it made; the ELF image (bzip2) runs
inside that process; and `AT_BASE` carries the faced runtime's own base, which
its own startup laid down, so the veneer thunks resolve. This is the same
real-process bring-up DR-0060 named for reent — hosting the runtime and bringing
reent up are one act, not two.

This supersedes, for the acceptance crossing, the arrangement DR-0060/DR-0066/
DR-0067 left in place — a host-Cygwin stub that loads the faced runtime beside
its own — and it sets aside the parent window handover (DR-0028) and its
cygwin-child reconcile (DR-0068/DR-0069) for this shape: a process that is the
faced runtime does not receive its low window from a parent, it lays its own
address space at startup and maps into it through its own `mmap`. Those
decisions stand for the plain-PE shape the WP-41 exec-* certifications drive;
this decision governs the faced-runtime shape the run halts on.

The A′ (a native stub that LoadLibrarys the faced DLL and calls it) and B′ (a
two-process split, the faced runtime in a separate host reached across a process
boundary) candidates the park had accumulated are not taken: the first has no
`_dll_crt0`-initialized thread, the second cannot serve in-process libc calls
across a boundary. Cygwin's own model — a program is the process whose sole
runtime is brought up at its own startup — dictates the shape, and the project
had already measured it in `spike/reent-bringup/` and recorded it in DR-0060.

## Consequences

The finishing work of the `reent-tls-bringup` rung is to move `accept.sh`'s
`build_loader` crossing off the host-Cygwin stub plus separate faceload onto a
crossing host that is a real process of `elfsysv1.dll` — linked so `_dll_crt0`
brings the faced runtime up as the sole runtime, its `mmap` performing the
DR-0008 mapping — and to run bzip2's image inside it. This touches the
WP-41-certified entry path and the DR-0008 fork-replay contract, so it is built
and certified as its own step, not folded into an existing spike; the plain-PE
exec-* bar must remain unregressed. The next measurement on this path is whether
a real process of the faced runtime, on its `_dll_crt0`-brought-up main thread,
maps a fixed low region through its own `mmap` where the parent handover to a
foreign child was refused; that measurement, not another window reconcile, is
the one the resolved shape turns on. The `to-green.tsv` `reent-tls-bringup`
signal stays `-` until a reent-consuming body reached through this crossing
returns its result, per DR-0060.
