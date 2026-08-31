# 0001 — WP-61 reopened: fault path fails on Cygwin 3.6.10

Raised 2026-08-30. WP-61 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/coredump/t/run.sh` fails (7 passed, 8 failed): the core writer
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
