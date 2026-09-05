# The red zone across a synchronous fault

Does NT write into the 128 bytes below the faulting thread's `%rsp` while it
dispatches an exception to a vectored handler on that thread?

No. Over a thousand first-touch faults on an uncommitted page, resolved by
committing and re-executing the store, and a thousand `int3` breakpoints
stepped over, not one byte of a painted red zone changed. NT placed the
`EXCEPTION_RECORD` 568 bytes below the interrupted `%rsp` and the `CONTEXT`
1832 below, so the dispatch frame begins more than 400 bytes under the red
zone's floor. A control in which the handler itself flips one byte at
`rsp-8` is caught every time, so the watcher can see. `results-2026-09-05.txt`
is the transcript; `finding=redzone-intact-through-fault-dispatch`.

## Why it matters

DR-0050 retired `-mno-red-zone` on evidence from the asynchronous delivery
path: a hijack from another thread, where the kernel builds the frame and
leaves the 128-byte gap (spike 38 confirms it). Substrate N's address space
also takes synchronous faults on the faulting thread, through the same
vectored handler that commits a lazily reserved page (spike 37, q6), and the
review of proposal 0011 argued that NT's dispatch, built for an ABI with no
red zone, would write its records directly below `%rsp` and corrupt a leaf's
temporaries before the handler ran. Had that held, N's toolchain would have
needed `-mno-red-zone` back, or eager commit for all anonymous memory. It
does not hold, and DR-0050 stands for both paths.

**Gates.** Proposal 0012's treatment of N's lazy commit and synchronous
signals; DR-0050.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Native, built with `x86_64-w64-mingw32-gcc` (`MEASURE_CC` overrides), under a
second. `-n` sets faults per case (1000).

## Method

`redzone.S` is the leaf: entered MS x64 with the page in `%rcx`, it paints
the 128 bytes below its `%rsp` with their own offsets, takes the event, and
reads the paint back, counting changed bytes and recording the farthest
offset that changed. It pushes nothing and calls nothing, so the only writer
below its `%rsp` is whoever dispatched the event. Three events: a store to a
page reserved but not committed (kind 0), `int3` (kind 1), and the same store
to a committed page (kind 2, the control that the leaf does not corrupt
itself). `redzone-probe.c` installs a vectored handler that commits the page
and continues, or steps over the breakpoint, and records where the
`EXCEPTION_RECORD`, the `CONTEXT` and its own frame sit relative to the
interrupted `%rsp`. A fourth run has the handler flip one byte at `rsp-8`, so
that an intact result is known to be a result and not a blind instrument.

## What this does not reach

One Windows build. The dispatch frame's position is what this kernel does,
not a documented guarantee; the presence check that would notice a change is
this spike's rerun. A fault taken while the stack is within a page of its
guard, where the dispatch itself needs the stack to grow, was not tried. Only
a vectored handler was measured, not a frame-based `__except`, and not a
delivery that continues somewhere other than the faulting instruction.
