# DR-XXXX — the faced DLL is exercised as a process's sole Cygwin runtime

Accepted 2026-08-31. Source: `doc/proposals/sole-runtime-crossing.md`.

## Context

WP-27's crossing and hostload tests were Cygwin programs that `LoadLibrary`'d
`elfsysv1.dll` into their own process -- a process that already held
`cygwin1.dll`. Cygwin permits one of its runtimes per process, sharing one
cygheap, TLS slot and process state; a second runtime in the same address space
is the configuration production never uses and the one prone to failure. It
passed on a quiet machine but flaked under load with `LoadLibrary` 126 and 998,
and the faced DLL's init ran a fork-style cygheap copy against the first runtime
that logged `child_copy: cygheap read copy failed ... Win32 error 299`.

## Decision

The against-the-DLL half of these tests is built native (mingw), not against
`cygwin1.dll`, and launched from `cmd` per the standing practice, so
`elfsysv1.dll` is the sole Cygwin runtime of a fresh process -- the shape the
product ships. The pure leaf exports the crossing calls (`strlen`, `labs`,
`memcmp`, `atan2`, `ldexp`) and hostload's DllMain and TLS observations need no
live Cygwin service, so a native caller drives them; a caller across the ABI
uses fixed-width types (`int64_t`, not `long`) because the System V `long` the
face presents is 64 bits and a Windows-compiled caller's is 32.

## What it does not change

The DLL itself needs no change -- it already relocates and loads as either a
sole or a second runtime, and the two-runtime run passing on a quiet machine was
correct, only measured in a configuration the product does not use. This makes
the certification faithful to the shipped shape and robust under load; it does
not overturn the earlier pass. The coexistence of two runtimes in one process
stays unsupported by Cygwin and out of scope.

## Where it is implemented

`runtime/face/t/crossing.{c,sh}` and `runtime/face/t/hostload.sh` on the WP-27
branch; it lands with WP-27. Verified there: crossing 8/8 and hostload with no
error 299 across repeated runs. This record lands ahead of it on the trunk.

## A gap it surfaced

Fixing the caller's `long` width exposed that `elfsysv1.dll`, compiled LLP64,
presents an LP64 `long` while its body reads 32 bits -- a truncation for `ftell`,
`strtol`, `sysconf` and kin past 2³¹. That is a face-classification concern, not
this decision's, and is recorded in `runtime/face/issue/0001`.
