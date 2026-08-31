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

## Diagnosed, 2026-08-31 — SIGPIPE at teardown, a WP-60 build defect

The exit-141 was traced (`toolchain/gdb/t/startup-sigpipe-trace-2026-08-31.txt`
is the `strace`). The binary is not missing a DLL — `cygcheck` resolves every
one — and the death is not a caller artifact: it reproduces identically with
stdin, stdout and stderr each redirected to a file and under `setsid`, so no
pipe or tty of the harness is involved. The Cygwin exit status is `0xD00`,
which is termination by signal `0x0D` = 13 = SIGPIPE.

What the trace shows: early in startup gdb's own Cygwin pipe transport fails
to connect — `transport_layer_pipes::connect: Error opening the pipe (2)` on
`\\.\pipe\cygwin-...-lpc` — and at process teardown gdb sends SIGPIPE to
itself (`sig_send ... signal 13, its_me 1`), which resolves to
`signal_exit: exiting due to signal 13` before any output is flushed. So the
binary writes into an internal pipe whose far end is already gone and does not
carry SIGPIPE ignored the way a debugger normally must. `--version`,
`--batch`, `-nx` and `show version` all die the same way at the same point.

This is a defect in how WP-60 built or configured gdb (its host-side signal
disposition, or an unwanted Cygwin-native transport compiled into a
cross-only debugger), not in fault delivery and not in WP-61. Fixing it means
reopening WP-60. A rebuild through `toolchain/gdb/build-gdb` is itself
currently blocked: the pinned GMP 6.2.1 that `build-gdb` compiles for the host
does not configure and build cleanly under the primary root's gcc 14.4, which
is a toolchain-pin question of its own. WP-61 stays held on a working cross
gdb; the blocker is now named rather than guessed.

## Closed, 2026-08-31 — the gdb was rebuilt on the primary root

The earlier rebuild failure was self-inflicted: a `CFLAGS` override forced onto
`build-gdb` broke GMP's configure. Without it, `build-gdb` builds GMP and MPFR
and then gdb cleanly under the primary root's gcc 14.4, `GDB_BUILD_RC=0`. The
rebuilt gdb runs -- `--version` returns and exits zero through both `cmd` and a
Cygwin shell, where the old binary died 141 -- and reports itself configured
`--target=x86_64-elfsysvnt-linux-gnu`. The SIGPIPE-at-teardown was a property of
a gdb built off the primary root, which DR-0038 already requires everything be
built on; building it there is the fix.

Re-run on the primary root, `runtime/coredump/t/run.sh` passes **15 of 15** --
every `readelf:` check and every `gdb:` check, the ones this report recorded as
failing among them. The hold's condition, a working cross gdb, is met, so WP-61
is delivered. This issue is closed.
