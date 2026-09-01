# placement-time reconciliation of the low window (WP-56 item 1, last step)

WIP. The reservation-time half of the low-window reconciliation landed as
DR-0068: the parent's `elf_window_reserve_in` recognizes a cygwin-linked
child's own low `MEM_RESERVE` region and reserves only the free remainder of
the window around it, with the decision certified as the pure `elf_window_plan`.

This step is the placement-time half the README's item 1 names last. When the
window was armed that way it is no longer a single reservation: it is the
child's own low region plus the parent's per-gap reservations. `elf_window_yield`
released the window with one `MEM_RELEASE` of the whole span and `elf_window_adopt`
confirmed a single covering reservation, both of which assume the pre-DR-0068
one-allocation window. This step teaches the placement side to account for a
window carved into several reservations: yield releases each constituent before
it bares the span for the placer, and adopt accepts a window fully covered by
one-or-more reservations with no committed occupant.

The decision that says which allocations compose the window and must be
released is `elf_window_release_plan`, certified as a pure decision in
`loader/exec/t/unit.c` beside `elf_window_plan`. The remaining live step is
unchanged from the README: driving the whole handover through a real
cygwin-linked child and measuring the reent-consuming body across the crossing.
