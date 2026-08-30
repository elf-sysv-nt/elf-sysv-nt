# DR-0021 -- the C3 carrier word is the floor of a runtime-owned stack, not a blind offset below StackBase

Status: accepted
Date: 2026-08-30
Deciding: WP-30's implementer; the re-measurement DR-0003 delegated, not one of the reserved three
Proposal: none; taken while building WP-30 against the real _my_tls

## What was decided

Carrier C3 stands: the thread pointer is a runtime-owned word reached through
`gs:[NtTib.StackBase]` then a fixed offset, exactly as DR-0003 settled. This
record fixes where that word lives, which DR-0003 left to the re-measurement it
delegated to WP-2x/WP-30.

The word is the floor of a stack this runtime owns. A managed thread runs on a
stack allocated with `mmap` and fully committed, so `NtTib.StackBase` is the top
of the allocation and the carrier word sits a build-constant offset below it —
sixteen bytes above the allocation floor — beneath the `_cygtls` reservation at
the top and far beneath the working `rsp`. This is not a reversal of DR-0003 and
does not reopen it; it is the placement the model always needed and the spike
stand-in deferred.

## Why not the stand-in's offset

`spike/gs-thread-pointer` wrote its stand-in word one page below `StackBase` and
said plainly it had not measured the real block. WP-30's re-measurement
(`runtime/tls/measure/`, transcript `results-2026-08-30.txt`) took the two
numbers DR-0003 named, and a third that follows from them.

The padding constant is real and larger than the guess. `cygwin_internal`
returns `CYGTLS_PADSIZE = 0x3200`, so the real `_cygtls` occupies
`[StackBase - 0x3200, StackBase)`. One page below `StackBase` is inside that
live block, which the stand-in's offset would have corrupted; it survived the
spike only because the spike drove a thread that had not populated its block
that far. The stand-in offset is unsafe against the real block.

There is no free owned word below `StackBase` on a real Cygwin. Mapping the
reservation shows the main thread's `_cygtls` words near `StackBase` are
Cygwin's live signal state — only `StackBase-8` is stable-zero there — and below
the reservation is working, descending stack. Squatting a fixed distance below
`StackBase` in memory Cygwin owns is safe only by luck. Owning the stack removes
the luck: the floor of a committed allocation is memory this runtime owns, below
both the reservation and the working stack, and reached by the same `%gs` chain.

## The alternate signal stack, settled in C3's favour

DR-0003's second carried question was what happens when Cygwin moves a thread
onto an alternate signal stack. Measured: Cygwin moves `rsp` onto the alternate
buffer but leaves `NtTib.StackBase` unmoved (`altstack.base_moved = 0` on the
main thread and on a worker). A carrier keyed to `StackBase` is therefore the
same word inside a `SA_ONSTACK` handler as outside it, so signal delivery needs
no re-establishment, and WP-30's acceptance test confirms the read end to end on
both delivery paths. This is a finding, not a new risk: the chain holds where
DR-0003 worried it might not.

## What it costs, and what it defers to the forked runtime

The read is unchanged from DR-0003: load `StackBase` from `%gs`, subtract the
offset, load the word. Owning the stack costs one `mmap` per managed thread and a
trampoline that establishes the pointer before the thread body runs; the read
path pays nothing for it.

This is the same contract the forked `elfsysv1.dll` will carry, established the
same way. There the carrier becomes a reserved field inside the runtime's own
`_cygtls`, and the offset below `StackBase` is measured against `sizeof(_cygtls)`
rather than against the stack. `elfsysv_tp_get` and the variant II `tcbhead_t`
do not change between the two; only what backs the word does. The offset of that
reserved field is the one piece this record leaves to the forked runtime, and it
is a placement inside a struct this project owns, not a fact about Cygwin left
unmeasured.

The measurement ran on the pinned root, Cygwin 3.0.7. DR-0007 records the
runtime is based on 3.6.10, whose `CYGTLS_PADSIZE` may differ. WP-30 reads the
constant from the running Cygwin at init through `cygwin_internal` and refuses to
start if the reservation has grown past the carrier offset, so the placement
tracks the running kernel rather than trusting `0x3200`; the number is the
measured value on 3.0.7, not a baked assumption.

## When to reopen this

Reopen if the forked `_cygtls` cannot spare a reserved field at a stable offset,
or if a Cygwin build grows `CYGTLS_PADSIZE` past what a managed stack can clear
and the init check begins refusing to start. Reopening means a new record
pointing back at this one. Do not edit this one.

## Where it is written down

`runtime/tls/README.md` and `runtime/tls/measure/README.md`, with the transcript
in `runtime/tls/measure/results-2026-08-30.txt`. DR-0003, which delegated this
re-measurement and which this record answers without reversing.
