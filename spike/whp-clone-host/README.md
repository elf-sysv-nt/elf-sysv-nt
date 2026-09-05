# Cloning a process that holds a partition

Does `RtlCloneUserProcess` of a Win32 process that owns a WHP partition
produce a child that runs? What of Win32 works in that child, can it build a
partition of its own and run a vCPU, what does the partition handle it
inherited do, and what does the clone cost with guest memory mapped?

The child runs. Heap, events, waits, `VirtualAlloc` and TLS work;
`LoadLibrary` does not. The child creates a partition, maps memory, creates
a vCPU and runs it to a halt in about 1.3 to 2.3 ms. A `WHvRunVirtualProcessor`
on the inherited handle as the child's first WHP call never returns; the same
call after the child has built its own partition returns a halt, which means
the child ran one iteration of the parent's guest. The clone costs about 3 to
5 ms with 4 MB mapped and 6 to 8 ms with 256 MB of touched memory, and the
control with that memory touched but unmapped costs the same: mapping into
the hypervisor adds nothing to the clone. `results-2026-09-05.txt` is the
transcript.

## Why it matters

Shape A of substrate H, one host process per Linux process, forks by cloning the host,
as N does, and the host under H is a Win32 process holding a partition,
because `WinHvPlatform.dll` needs `kernel32`. Spike 35 (`nt-clone-fork`)
cloned a plain native process. Whether a clone that has the hypervisor's
user-mode state in it, and its parent's partition handle in its handle table,
runs at all was the fourth measurement that note listed, and the one that
decides whether shape A is available.

**Gates.** Shape A's viability, and its fork floor, in proposal 0012.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Native, built with `x86_64-w64-mingw32-gcc` (`MEASURE_CC` overrides).
Unelevated; a run takes about twenty seconds, most of it the eight-second
wait on the child that hangs by design in q2a. `-n` sets the clones per
timing case (20).

## Method

The parent brings up a partition to its first halt (the ring-0 `hlt` loop
from `spike/whp/`), creates an inheritable section and event, and calls
`RtlCloneUserProcess` with `INHERIT_HANDLES`. The child branch touches no C
runtime state: it writes its findings as words into the shared section,
signals the event, and ends with `NtTerminateProcess`, so that the runtime a
clone inherits is not itself part of the measurement. Each field of the
report is a word so a missing step reads as its zero, and a step counter says
where a child that hangs got to.

q2a's child runs the inherited partition handle first. q2's child runs Win32
first (heap, event, wait, `VirtualAlloc`, TLS, `LoadLibrary`), then builds a
partition of its own to a first halt, timed, then tries a second mapped
partition beside it, then signals, and only then runs the inherited handle;
the parent polls for that last result for five seconds and reports a hang if
it does not come. q3 runs the parent's own partition afterwards. q4 times
twenty clones whose children exit at once; q5 maps 256 MB of touched memory
into the parent's partition and times again, then repeats q2 with that
mapping held; q5b unmaps it and times once more as the control.

## What the transcript says, read for the design

Shape A's fork is available. A clone of a partition-holding Win32 process
runs, does ordinary Win32 work, builds its own partition and runs a vCPU.
Its cost is the clone (3 to 8 ms here, scaling with the parent's touched
memory exactly as it does without any hypervisor involved) plus 1.3 to 2.3 ms
to bring the child's partition to its first exit, twice what a fresh process
pays in `spike/whp-partition-cost/`, and then the child's own remapping of
every backed range, which this probe does not do.

Two things the child must do first. `WinHvPlatform.dll`'s state in a clone is
not usable until the child has made a call that reinitialises it; a run on
the inherited handle before that never returns, and a run after it drives
the parent's guest, so the design closes the inherited handle before
anything else and never runs it. And `LoadLibrary` fails in the clone, which
a host that loads nothing after start can live with, and which is one more
reason the host under any shape links what it needs at build time.

The one-mapped-partition rule from `spike/whp-partition-cost/` holds in the
child too, and the inherited mapped partition does not count against it: the
child's own partition maps, a second does not.

## What this does not reach

Why `LoadLibrary` fails in the clone was not diagnosed; the loader lock, the
stale `csrss` connection and the copied loader lists are all candidates.
Whether the hang on an inherited handle is a lock in `WinHvPlatform.dll` or
in the VID driver was not looked at. The child never remaps the parent's
guest memory into its own partition, which is the part of shape A's fork
that would scale with the address space. The clone's cost was measured with
the child terminating at once, so it is the parent-side cost only. One host,
one Windows build, one AMD part.
