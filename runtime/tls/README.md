# The thread pointer (WP-30)

This unit establishes the thread pointer at thread creation and wherever the
host can disturb it, and reads it back. The model is DR-0003's: the pointer is a
word this runtime owns, kept a fixed distance below the thread's stack base and
reached through `%gs` as `gs:[NtTib.StackBase]` then that offset, carrier C3 of
`spike/gs-thread-pointer`. The TCB behind the pointer is the psABI variant II
shape, `tcbhead_t` at the pointer and the static TLS block at negative offsets.

`tls.h` is the interface: the `tcbhead_t`, the carrier read and write, the TCB
allocator, and managed-thread creation. `tp.c` is the body. `t/` is the
acceptance test and its certification harness. `measure/` is the re-measurement
DR-0003 requires, taken against the real `_my_tls` rather than the spike's
stand-in, with its committed transcript.

## What the read costs, and why it is a chain

On Linux the thread pointer is the segment base and one instruction reaches a
variable. Here `%gs` is NT's TEB, which this runtime does not own and cannot
clear, so the pointer is a word fetched out of a structure NT maintains. A read
is a load of `NtTib.StackBase` from `%gs`, a subtract of the carrier offset, and
a load of the word. DR-0003 priced C3 at about 5.5 cycles against a global's
2.5, and against the only other working option, emulated TLS at 33.7, that is
the cheap end. `elfsysv_tp_get` is that sequence and nothing more.

## Where the carrier word lives, and why not one page below StackBase

The spike stand-in wrote its word one page below `StackBase`, a blind offset it
was honest about carrying. The re-measurement (`measure/`) shows why WP-30 does
not: the real `_cygtls` reservation is `CYGTLS_PADSIZE = 0x3200`, not a page, so
one page below `StackBase` lands inside Cygwin's live block, and on the main
thread the words near `StackBase` are Cygwin's own signal state, which a carrier
must not overwrite. Below the reservation is live and descending stack. On a
real Cygwin, whose `_cygtls` this runtime does not yet own, there is no free
owned word below `StackBase` that is neither Cygwin's block nor working stack.

So the runtime owns the word by owning the thread's stack. A managed thread runs
on a stack this unit allocates with `mmap` and fully commits, which makes
`NtTib.StackBase` the top of that allocation and the whole allocation writable
with no Windows guard-page growth to trip. The carrier sits sixteen bytes above
the allocation floor — `ELFSYSV_TP_CARRIER_OFF` below `StackBase` — beneath the
reservation at the top and far beneath the working `rsp`, which the measurement
confirms is the safe region. The reasoning and the numbers are DR-0021.

This is the same contract DR-0003 names, established the same way it will be in
the forked `elfsysv1.dll`, where the carrier becomes a reserved field inside the
runtime's own `_cygtls` and the offset below `StackBase` is a build constant.
`elfsysv_tp_get` and the `tcbhead_t` layout do not change between the two; only
what backs the word does.

## The three disturbances the acceptance test drives

A hundred thousand context switches under load. Two threads per processor, each
with a distinct TCB, spin verifying that `elfsysv_tp_get` still returns their own
`tcbhead_t` while the scheduler migrates and preempts them. The switch count is
the kernel's own, read off `NtQuerySystemInformation` the way the spike reads it,
so the threshold is real switches and not yielded iterations. The run clears a
hundred thousand switches with zero mismatches.

A fork. A managed thread forks; the child keeps only that thread, re-establishes
the pointer on the sanctioned post-fork path, and reads its TCB back. The
measurement shows the child inherits the same `StackBase` and the copied carrier
word, so the pointer already reads back; the re-establishment is explicit anyway,
because the forked runtime will allocate a fresh block there.

A signal delivered mid-computation. A handler reads the pointer while a live
value sits in registers, both on the thread's own stack and on an alternate
signal stack. The measurement settles the alternate-stack question DR-0003 left
open: Cygwin moves `rsp` onto the alternate stack but leaves `NtTib.StackBase`
unmoved, so the carrier keyed to `StackBase` is the same word the thread
established, and the handler reads the correct TCB. No re-establishment is needed
at signal entry, and the test confirms it for both delivery paths.

## Running it

    ./t/run.sh                 # build, check variant II offsets and -mno-red-zone, accept
    ./t/run.sh -n 1000000      # a larger context-switch target
    ./measure/measure.sh -o results-$(date +%F).txt   # re-measure _my_tls

The unit is built with the host toolchain and `-mno-red-zone`, the standing
policy DR-0006 carries until the delivery-site repair in WP-43.

## What this does not do

The static TLS block is allocated and addressable here; sizing it from the
initial `PT_TLS` set, the dtv, `__tls_get_addr`, and teardown are WP-37, which
needs this unit and the relocator. The carrier is proven against a real Cygwin
whose `_cygtls` this runtime does not own; the co-location of the word inside the
forked `_cygtls`, and its offset measured against `sizeof(_cygtls)` rather than
against the stack, is the piece the forked runtime finishes, and DR-0021 records
what it inherits from here.
