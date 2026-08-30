# Re-measuring the real _my_tls

DR-0003 settled carrier C3 against `spike/gs-thread-pointer`, and recorded that
the spike measured a stand-in rather than Cygwin's real `_my_tls`. It named two
things WP-30 must re-measure against the real block as it builds: the padding
constant, and the block's behaviour when Cygwin moves a thread onto an alternate
signal stack. This is that re-measurement. It is kept on the same terms as a
spike: rerunning `measure.sh` regenerates the transcript, and a probe that no
longer runs is a defect the way a failing test is.

`measure.sh` builds and runs three probes and writes `results-<date>.txt`.

## The probes

`remeasure-my-tls.c` reads the padding constant straight from the running
Cygwin — `cygwin_internal(CW_CYGTLS_PADSIZE)`, which returns `CYGTLS_PADSIZE`
itself — and reports the geometry below `StackBase` on the main thread, a fresh
thread and a fork child, plus whether `NtTib.StackBase` moves inside a handler
delivered with `SA_ONSTACK`.

`map-cygtls.c` walks the `_cygtls` reservation one word at a time and reports,
per thread, which words are zero at thread start and unchanged by a signal, so
the boundary between Cygwin's used region and its pad is measured rather than
guessed.

`owned-stack.c` confirms the mechanism WP-30 actually uses: a stack allocated
with `mmap` and handed to `pthread_attr_setstack` makes `NtTib.StackBase` the
top of the allocation, its floor is a writable owned slot below the reservation
and far below `rsp`, and `fork` from such a thread works with the carrier word
intact in the child.

## The verdict, 2026-08-30

Taken on Windows 10.0.26200.9168 under the pinned root, Cygwin 3.0.7, gcc 7.4.0,
recorded in `results-2026-08-30.txt`.

The padding constant is `CYGTLS_PADSIZE = 12800`, `0x3200`. This is more than
three times the spike stand-in's blind one page, and it settles the first open
number: the real `_cygtls` reservation is `[StackBase - 0x3200, StackBase)`, so
a carrier one page below `StackBase` — the stand-in's offset — lands inside
Cygwin's live block. The stand-in offset is unsafe against the real block, which
is exactly the divergence DR-0003 reserved the right to find.

The reservation is not free pad near `StackBase`. `map-cygtls` shows the main
thread using its `_cygtls` words down from `StackBase` — only the single word at
`StackBase-8` reads back zero and unchanged across a signal — while a fresh
thread leaves about five kilobytes near `StackBase` untouched. Cygwin's live
signal state sits at the top of the block on the thread that has done signal
setup, so a carrier cannot be squatted there in general. Below the reservation
is working stack. On a real Cygwin there is no owned word below `StackBase` that
is neither Cygwin's block nor descending stack.

The alternate signal stack does not move `NtTib.StackBase`. Delivered on a
`sigaltstack`, the handler's `rsp` is on the alternate buffer, but
`NtTib.StackBase` reads the thread's original base unchanged on both the main
thread and a worker (`altstack.base_moved=0`). This settles the second open
question in C3's favour: a carrier keyed to `StackBase` is reached identically
inside a signal handler, on the alternate stack or not, so signal delivery needs
no re-establishment. WP-30's acceptance test confirms the read end to end.

The consequence for WP-30 is DR-0021: the carrier is owned by owning the
thread's stack and placing the word at its committed floor, below the
reservation and below the working stack, rather than squatting a fixed distance
below `StackBase` in memory Cygwin owns. What this cannot measure is the forked
`_cygtls` itself, which does not exist until `elfsysv1.dll` is built; the offset
of the reserved field inside it, measured against `sizeof(_cygtls)` rather than
against the stack, is the piece the forked runtime finishes.

## Reproducing it

    ./measure.sh -o results-$(date +%F).txt

The counts belong to the machine and the minute. The constants are what travel:
`CYGTLS_PADSIZE = 0x3200`, `altstack.base_moved = 0` on every thread, and a
runtime-owned stack carrying the word at its floor without collision.
