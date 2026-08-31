# 0001 — WP-61 reopened: fault path fails on Cygwin 3.6.10

Raised 2026-08-30. WP-61 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/coredump/t/run.sh` fails (7 passed, 8 failed): the core write
itself runs and refusals refuse, but the fault-driven paths do not complete as
recorded.

A separate, now-fixed harness detail: the test looked for the cross `readelf`
and WP-60's gdb at `$HOME/x-elfsysvnt`, which resolved only in the rhel root
until the toolchain moved to `/c/-/x-elfsysvnt` with a `~/x-elfsysvnt` symlink
(DR-0038). With that in place the tool-not-found failures clear; the remaining
failures are the fault path itself.

## Root cause

The fault-under-a-System-V-frame divergence in `spike/abi-crossing/issue/0001`:
a fatal signal's core is written on a path that crosses a System V frame, and
3.6.10 delivers that fault differently than 3.0.7. Shared with WP-22 and WP-43.

## Status

WP-61 is un-delivered and held; the worker will not attempt it. Its redo waits on
the operator's 3.6.10 fault-delivery characterization spike. Source retained;
certification withdrawn on the real environment.

## Characterized, 2026-08-31

The characterization is in `spike/abi-crossing/issue/0001`, and the root cause
above does not survive it twice over.

First, there is no fault-under-a-System-V-frame divergence to inherit. Measured
on both roots, that crossing behaves identically; what differs is that gcc 14
removes a fault written as a store through a literal null pointer, which is how
`spike/abi-crossing` and `runtime/core/t` raise theirs.

Second, this suite raises no fault at all. `runtime/coredump/t/corewrite_test.c`
synthesizes an `ET_CORE` file from canned register values and reads it back;
nothing runs beneath a System V frame and nothing crashes. So the shared cause
was never available to it.

The eight failures re-measured on the primary root on 2026-08-31 are all and
only the `gdb:` checks, and they have one cause of their own: the cross gdb is
broken on this host.

    $ $HOME/x-elfsysvnt/bin/x86_64-elfsysvnt-linux-gnu-gdb --version
    exit 141, 0 bytes of output

It dies on `--version`, with output redirected to a file, before it can say
anything. `have "$GDB"` finds the file, so the suite's tool-not-found branch is
not taken and every `grep` for expected gdb output matches nothing and reports
`bad`. All seven `readelf:` checks pass, so the core the writer produces is at
least well-formed ELF; whether its contents are right is untested on this root
because the reader is broken.

That is a toolchain defect and it belongs to WP-60, not to fault delivery.
Diagnosing why that binary dies is not this measurement's to do. WP-61 stays
held; what it is waiting on is a working cross gdb, not a fault-delivery
answer.
