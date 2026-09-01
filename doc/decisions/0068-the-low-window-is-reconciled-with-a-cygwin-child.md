# DR-0068 — the low window is reconciled with a cygwin-linked child, not reserved over it

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
item 1 of `acceptance/reent/README.md`.

## Context

DR-0028 has the parent reserve the `0x400000` low window into a suspended child
with a single `VirtualAllocEx`, before the child runs, and the child adopts what
it finds. That holds for a plain-PE stub. The real-process stub item 1 needs is
linked against `cygwin1.dll`, and `spike/reent-stub-realproc-window-occupant`
measured what that changes: walked at `CREATE_SUSPENDED`, before any user code,
the child already holds the low ~2 MB at `0x400000` as its own private
`MEM_RESERVE` region. The parent's whole-window reservation is then refused with
`err=487` (`ERROR_INVALID_ADDRESS`), because the span it asks for *starts* on a
region the child already owns. The collision is only the low ~2 MB; from
`0x600000` the window is free, and the plain-PE control's window is free
throughout and its handover still succeeds.

So the obstacle is not that the window is occupied by something foreign. It is
that the child reserves its own low region before the parent gets to, and a
single reservation cannot begin on ground already held.

## Decision

The reconciliation is identification, not eviction. The parent recognizes the
child's low reservation and reserves only the free remainder of the window,
rather than reserving over what the child holds or trying to evict it. The
region the child reserved is the same runtime the window exists to protect, so
leaving it in place loses nothing: it is already held against an arbitrary
allocation, which is all the reservation was for.

`elf_window_reserve_in` keeps the DR-0028 whole-window call as its fast path --
the plain-PE stub still takes it in one call, unchanged, so the WP-41 exec-*
certifications are undisturbed -- and adds a fallback taken only when that call
is refused. The fallback walks the child's window with `VirtualQueryEx` and
plans the reservation with `elf_window_plan`, a pure decision that emits the
free sub-spans the parent must still reserve, recognizes the child's own
`MEM_RESERVE` regions and leaves them in place, and refuses only a `MEM_COMMIT`
occupant, which is not a bare reservation and cannot be reconciled. The parent
then reserves each planned span on its own. Splitting the reservation is what
lets the low region stay the child's while the rest of the window is still held
for the image.

Keeping the planner pure is what makes the decision certifiable without a live
suspended child: `loader/exec/t/unit.c` holds it to the low window's own
constants -- the child's 2 MB reservation at the `0x400000` base recognized, a
committed occupant refused, interleaved reservations reduced to their free
spans -- as arithmetic, with no process existing anywhere.

## Consequences

The window handed to the child is no longer necessarily one reservation. When
the fallback runs, the low region is the child's and the remainder is the
parent's, so `elf_window_yield`'s later release-and-place must account for a
window it does not solely hold; that reconciliation at placement time, within
the child, is item 1's remaining finishing work, and it now rests on a measured
constraint rather than a guess. The `to-green.tsv` `reent-tls-bringup` signal is
unchanged: it wires to a reent-consuming body reached across the loader
(item 3), not to this reservation step.
