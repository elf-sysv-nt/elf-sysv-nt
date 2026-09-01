# DR-XXXX — the low window is reconciled at placement, not only at reservation

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
the last step of item 1 in `acceptance/reent/README.md`. Follows DR-0068.

## Context

DR-0068 split the parent's reservation of the low window. Against a plain-PE
child the parent still reserves the whole `0x400000` window in one
`VirtualAllocEx`; against a cygwin-linked child, whose own low ~2 MB reservation
is already held before any user code runs, the parent recognizes that region and
reserves only the free remainder around it. The window handed to such a child is
therefore no longer one reservation: it is the child's own low region plus the
parent's reservations above it.

The placement side had not caught up. `elf_window_yield` released the window with
a single `VirtualFree(base, 0, MEM_RELEASE)` and re-reserved two remainders
around what the placer took, and `elf_window_adopt` confirmed the window by
checking that one reservation covered it. Both assume the pre-DR-0068
one-allocation window. Against a reconciled window the single release frees only
the allocation whose base is `0x400000` and leaves the parent's reservations
above it standing, so the image's brk and any near placement collide with ground
still held; and adopt refuses a window it should accept, because no single
reservation spans it.

## Decision

Teach the placement side that the window may be several reservations, each its
own `MEM_RELEASE` unit.

`elf_window_yield` surveys the window with `VirtualQuery` before it releases
anything, then releases each constituent reservation rather than the base alone.
Which reservations those are is `elf_window_release_plan`, a pure decision beside
`elf_window_plan`: given the window's regions as the query reports them, it names
the base of each reserved region within the window, skips a free hole as nothing
to release, and refuses a `MEM_COMMIT` occupant or a reservation that overruns
either window edge, neither being the window's to free. With the constituents
released the span is bare, the placer runs against the same bare address space
DR-0028 requires, and the untaken remainder is re-reserved as before. A
single-reservation window plans to exactly one release, so the plain-PE and
self-window paths behave as they did.

`elf_window_adopt` confirms coverage instead of a single reservation: it walks
the window and accepts it only if every byte stands `MEM_RESERVE` with no free
hole and no committed occupant, which a reconciled multi-reservation window
satisfies and a stub started with no parent to arm it does not.

Keeping the release decision pure is what makes it certifiable without a live
suspended child, as with DR-0068's planner. `loader/exec/t/unit.c` holds
`elf_window_release_plan` to the low window's own constants: a single covering
reservation released as one base, the child's low region and the parent's
remainder released as two, a free hole skipped, a committed occupant refused, a
reservation overrunning either edge refused.

## Consequences

The exec path is undisturbed where it was already right: the WP-41 `run.sh` bar
-- unit, fuzz, the reservation `when`, exec-elf/script/chain/loop/nonelf,
exec-kind, dyn-cross, dyn-init -- passes unchanged, because against the plain-PE
child the window is one reservation and the new code plans one release and
confirms one covering span. What the change unblocks is the reconciled window a
cygwin-linked child presents, whose release-and-place now accounts for the
child's own low region rather than assuming the parent holds the whole span.

This does not wire the `to-green.tsv` `reent-tls-bringup` signal. That signal
stays on a reent-consuming body reached across the loader (item 3). What remains
of item 1 is the live measurement: driving the whole handover -- reserve around
the child, adopt the reconciled window, yield and place within it -- through a
real cygwin-linked child rather than the in-process unit fixtures, which is the
next step recorded in `acceptance/reent/README.md`.
