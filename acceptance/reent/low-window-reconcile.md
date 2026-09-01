# reconciling the low-window handover with a cygwin-linked child (WP-56 item 1)

WIP. Item 1's last step, per `acceptance/reent/README.md`.

`spike/reent-stub-realproc-window-occupant` measured the obstacle: a
cygwin-linked child, walked at `CREATE_SUSPENDED` before any user code runs,
already holds the low ~2 MB at `0x400000` as a private `MEM_RESERVE` region.
The parent's DR-0028 `VirtualAllocEx` of the whole `0x400000` window into the
child is therefore refused with `err=487` (`ERROR_INVALID_ADDRESS`), because it
*starts* on the child's own low reservation. The collision is only the low
~2 MB; from `0x600000` up the window is free.

The reconciliation is identification, not eviction: the parent recognizes the
child's low reservation and reserves only the free remainder of the window,
rather than reserving over what the child already holds. This note tracks that
change; the design record is `doc/decisions/` (the low-window reconciliation)
and the implementation is the reconciling fallback in `loader/exec/reserve.c`
with its planner certified as a pure decision in `loader/exec/t/unit.c`.
