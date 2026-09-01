# the low window reconciled against a real cygwin-linked child

WP-56's `reent-tls-bringup` rung, item 1. `spike/reent-stub-realproc-window-occupant`
measured that the raw DR-0028 handover — a single whole-window `VirtualAllocEx`
of `ELF_WINDOW` at `0x400000` into a suspended cygwin-linked child — is refused
(`err=487`), and read the collision as the child's *private `MEM_RESERVE` over
the low ~2 MB*. DR-0068 and DR-0069 answered that reading: `elf_window_reserve_in`
falls, on refusal, to a `VirtualQueryEx` walk plus the pure `elf_window_plan`
plus a per-gap reserve, recognizing the child's own low reservation rather than
reserving over it; `elf_window_yield`/`elf_window_adopt` do the placement-time
half. Those planners are certified as pure decisions in `loader/exec/t/unit.c`,
against fixtures that model the child's low region as one `MEM_RESERVE`.

Item 1's remaining owed step is the *live* measurement — driving that real
code against a real cygwin-linked child rather than the in-process fixtures.
This spike is the first of the four verbs, `reserve`, the half drivable
entirely parent-side.

## What it does

`reconcile-probe.c` spawns a stub `CREATE_SUSPENDED` — the moment `dispatch.c`
attempts the handover, before `ResumeThread` — and, against that live child:
records the raw whole-window `VirtualAllocEx` verdict (the occupant result
reproduced); calls the real `elf_window_reserve_in` (linked from
`loader/exec/reserve.c`, with its DR-0068 reconcile fallback); then walks the
window with `VirtualQueryEx` and reports whether every byte is now `MEM_RESERVE`
with no free hole (`window_covered`) and a stable classification of the
window's non-free content (`low_window_occupant`). `measure.sh` builds the
real-process stub (`-nostdlib` against the WP-26 `crt0.o` and `-lcygwin`) and
the plain-PE control (the WP-41 shape) and runs the probe on each. Addresses and
region sizes are volatile and print as `VirtualQuery`-style context the t3
runner strips; the reproducible findings are the words.

## The finding (results-2026-09-01.txt, reproduces)

`verdict=blocked-by-committed-occupant`. The reconcile does **not** yet clear
the refusal against a real cygwin-linked child, and the reason contradicts the
model DR-0068's planner was written against.

The plain-PE control reserves the whole window in one call
(`plain_reserve_in=win_ok`, `plain_window_covered=yes`) — the fast path and the
harness both work. The cygwin-linked child is refused the raw whole-window call
(`realproc_raw_whole_window=refused`), as the occupant spike found, and
`elf_window_reserve_in`'s reconciling fallback is refused too
(`realproc_reserve_in=win_err_refused`, `realproc_window_covered=no`). The walk
shows why: the child's low window is `low_window_occupant=reserved+committed`,
not the bare `MEM_RESERVE` the planner models. Below the free tail sit a private
reservation over the low span **and committed pages** just under it (at this
writing, a `~0x1fc000` reservation at `0x400000` and two committed regions up
to `0x600000` — the runtime's own, present before any user code). `elf_window_plan`
returns `-1` on any committed region overlapping the window — by design, "an
occupant that is not the child's own bare reservation and cannot be reconciled"
— so `reserve_in_around` returns `win_err_refused`.

## Where this points

Item 1's reserve verb does not clear a real cygwin-linked child, because the
child's low occupant is reserved **and committed**, while DR-0068/DR-0069
reconcile only a reserved one. The design step that follows is how the loader
should treat a committed low occupant that is the child's own runtime allocation
— and that is a decision with more than one live candidate (adopt the committed
region as the child's own; reserve only up to it and treat the span above as
best-effort; reserve around it and accept a non-contiguous window; something
else). It is parked for the operator rather than guessed here (`a/build-blockers.log`).
This spike fixes the constraint that decision must satisfy, measured rather than
inferred, and guards that the obstacle still reproduces; when a reconcile that
handles the committed occupant lands, the verdict flips to `cleared` and the t3
runner flags the change.

## Reproducing

`./measure.sh` from this directory, or `test/t3-regen.sh`. It SKIPs
(`verdict=yes`) when the WP-26 build tree (`crt0.o`, `libcygwin.a`) is absent,
those being uncommitted build products. The reproducible findings are the
classification words — `raw_whole_window`, `reserve_in`, `window_covered`,
`low_window_occupant`, and the `verdict`; addresses and region sizes ride along
as context and are stripped by the runner.
