# DR-0030 — the receiving thread builds the frame, and the return is an iretq

Status: accepted
Date: 2026-08-30
Deciding: WP-43, against measurements in `runtime/signal/t/`
Proposal: none; taken while writing WP-43

## What was decided

An ELF signal delivery on this platform has two shapes that were not obvious
before the code was written, and both are load-bearing.

The frame is built by the thread that receives the signal, not by the thread
that sends it. The hijack suspends the target, copies its register file into a
record on the heap, points its instruction pointer at `elfsysv_sig_enter` with
the record in `%r11`, and resumes it. Everything after that runs on the target.

The return from a handler is a same-privilege `iretq`. Every general register
is loaded from the saved file and the last three — instruction pointer, flags
and stack pointer — come from an iret frame built on the stack the runtime
already owns.

DR-0006's reservation is unchanged and is now implemented: the frame is placed
128 bytes below the interrupted stack pointer before anything else is
subtracted.

## Why the receiver builds it

Because the sender cannot. A Windows thread stack is reserved address space
with a guard page at its frontier, and touching that page is a stack extension
only for the thread the stack belongs to. A write from another thread reaches
the same page and takes `STATUS_GUARD_PAGE_VIOLATION` as an ordinary fault, in
the writing thread, where nothing is prepared to handle it.

This was measured rather than reasoned about. The first version of the delivery
built the frame from the sending thread and passed at twenty deliveries; at a
hundred it died with a guard-page violation on the sender's own instruction
pointer, in the sender's own stack trace, while writing into the target's.
Twenty deliveries fit in pages the target had already committed and a hundred
did not.

The repair is also the more faithful shape. Cygwin's own delivery works this
way: `sigdelayed` runs on the interrupted thread, and the hijack only decides
that it will run. The trampoline WP-43 is named for is exactly this, and the
first version had removed it.

`elfsysv_sig_enter` steps down one page at a time before it calls anything,
because moving a stack pointer past a guard page without touching it is how a
Windows stack fails to grow.

## Why the return is an iretq

Because the alternative writes into the red zone.

The conventional user-mode way to install a register file is `setcontext`'s:
load the stack pointer from the file, push the return address, and `ret`. That
push lands at the destination stack pointer minus eight, which is the first
word of the red zone — the same word spike 3 measured Cygwin's delivery taking
on every delivery. A package whose purpose is to keep those 128 bytes cannot
return through an instruction sequence that writes into them.

`iretq` takes the instruction pointer, the flags and the stack pointer from a
frame on the current stack, so it writes nothing at the destination at all. It
is not privileged: at CPL 3, returning to CPL 3, it is an ordinary instruction,
and in 64-bit mode it always pops SS:RSP, which is the property that makes it
work here. `t/iretq_probe.c` establishes both facts on this host and gates the
certification, so a machine where the instruction is unavailable fails at the
probe rather than somewhere further in.

The flags that come back are masked to the ones a user may set, since `iretq`
pops the field wholesale and the field arrives from a frame a handler could
have written.

## What the reservation costs

Nothing that can be measured in the path that will actually run.

`t/sig_e2e.c` times deliveries with the reservation and without it, over 500
deliveries per arm, through the host's real suspend, redirect and resume. On
2026-08-30 the reserving arm ran at 26 to 32 microseconds per delivery and the
control arm at 34 to 36, with the difference changing sign between runs: −23%,
−12%, −10%, +16%. A cost that is negative on most runs is not a cost. The
reservation is one subtraction inside a path whose price is three system calls
and a thread suspension, and it is several orders of magnitude below the noise.

DR-0006's bands read this as "under 5%: proceed", and this record proceeds. But
the honest reading is the one `spike/cygwin-from-source` reached first: those
bands could only ever return proceed, and the number was never the question.

What the number does not settle, and what this record does not claim, is
anything about Cygwin's own `sigdelayed`. WP-43 does not patch it. An ELF
process does not go through it: the hijack redirects into this package's
trampoline, and the frame this package places is the only frame built. The
measurement that DR-0006 sent here — the reserving `sigdelayed` against the
unmodified one — is a measurement of a path that has been replaced rather than
repaired, and `spike/cygwin-from-source/reserve-redzone.patch` remains the
record of how it would have been done had the delivery stayed Cygwin's.

## What the control arm establishes

That the measurement can see what it claims to see.

`spike/cygwin-from-source` failed on exactly this: its probe reported the
nearest write moving in the wrong direction by the size of ordinary variation,
which meant it was not watching `sigdelayed`'s frame at all. So the delivery
here carries a switch that puts the frame back at the interrupted stack pointer
and writes the word Cygwin's first instruction writes, and the certification
requires the leaf's accumulator to break when it is on. A run where the control
arm reports an intact red zone is a failed run, not a better one.

## What it costs to reverse

The receiver-builds-it decision cannot be reversed; it is a property of the
platform rather than a choice. Sending-thread construction would need the
sender to commit the target's stack pages itself, which means walking the
target's reserved region with `VirtualQuery` and `VirtualAlloc` while it is
suspended, and getting it wrong corrupts a running thread's stack.

The `iretq` return could be reversed for a `ret`-based one at the price of the
red zone, which is the thing DR-0006 decided to keep. A narrower reversal is
available and is not taken here: a `ret $128` after placing the return address
below the reservation works when the frame is on the interrupted stack, and
does not when it is on an alternate stack, because the distance back exceeds
what `ret imm16` can add. One return path that always works was preferred to
two that each work sometimes.

## Where it is written down

`runtime/signal/README.md` and the headers of `sigenter.S` and `sig_host.c`.
`doc/IMPLEMENTATION-PLAN.md`, WP-43. DR-0006, whose price this settles.

## Not verified

That `iretq` remains available under a hardened Windows configuration. Control-
flow enforcement, and shadow stacks in particular, interact with any return
that is not a plain `ret`, and this host has neither enabled. The probe checks
the machine it runs on and says nothing about a machine it has not run on.

That the delivery is correct when it lands inside `cygwin1.dll` rather than in
ELF code. Cygwin's own `interrupt_now` defers in that case, and this package
does not yet: it redirects wherever the target happens to be. Deliveries in the
certification landed in a hand-written leaf and in this package's own code, and
a target inside the host runtime holding a host lock has not been tested.

That the state is per thread. `elfsysv_sigstate_t` carries one blocked mask and
one alternate stack, and POSIX gives each thread its own. The state travels
through `elfsysv_sig_current`, which is per thread, but the certification uses
one state for the whole process. The per-thread split belongs with the runtime's
own `_cygtls` work.

That `SA_RESTART` restarts a real interrupted host call. What is certified is
`elf_sig_restart_after`, the decision a down-call wrapper consults, against
every disposition. The wrapper that will consult it is DR-0009's and is not
written yet.
