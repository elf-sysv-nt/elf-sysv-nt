# placement-time reconciliation of the low window (WP-56 item 1, last step)

Item 1's last non-live step, per `acceptance/reent/README.md`. Landed; the design
record is DR-XXXX (bound at land time), following DR-0068.

The reservation-time half landed as DR-0068: the parent's `elf_window_reserve_in`
recognizes a cygwin-linked child's own low `MEM_RESERVE` region and reserves only
the free remainder of the window around it, with the decision certified as the
pure `elf_window_plan`. The window handed to such a child is then no longer a
single reservation -- it is the child's own low region plus the parent's
reservations above it.

The placement side has now caught up. `elf_window_yield` surveys the window with
`VirtualQuery` and releases each constituent reservation before it bares the span
for the placer, rather than the one `MEM_RELEASE` of the base it did before; and
`elf_window_adopt` accepts a window covered by one or more reservations with no
committed occupant, rather than requiring a single covering reservation. The
decision naming which reservations to release is `elf_window_release_plan`,
certified as a pure decision in `loader/exec/t/unit.c` beside `elf_window_plan`:
one covering reservation released as one base, the child's low region and the
parent's remainder as two, a free hole skipped, a committed occupant refused, a
reservation overrunning either edge refused. The whole WP-41 `run.sh` bar (unit,
fuzz, when, the exec-* routes, exec-kind, dyn-cross, dyn-init) passes unchanged,
so the plain-PE and self-window paths -- one reservation, planning to one release
and one covering span -- are undisturbed.

The remaining step is unchanged from the README: driving the whole handover --
reserve around the child, adopt the reconciled window, yield and place within it
-- through a real cygwin-linked child rather than the in-process unit fixtures,
and then measuring the reent-consuming body across the crossing, which is what
wires the `to-green.tsv` signal.
