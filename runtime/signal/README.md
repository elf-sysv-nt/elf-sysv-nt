# signals (WP-43)

Cygwin delivers a signal by hijacking the target thread: it suspends it, reads
its register file, points it at a delivery stub and resumes it. What that stub
builds is a Cygwin frame for a Cygwin handler. An ELF process expects the frame
a Linux kernel builds, at the offsets the psABI and the Linux headers agree on,
and this package is that frame and the two ends of the path around it.

## The frame

`signal.h` declares it and `sigframe.c` asserts it. A return address, then
`ucontext_t`, then `siginfo_t`, with the extended FPU state above them and
`uc_mcontext.fpregs` pointing at it — Linux's `rt_sigframe`, in that order,
because a handler compiled against Linux headers reaches into it by offset. The
assertion that matters is that the interrupted stack pointer lands at
`ucontext + 160`, which is glibc's `oRSP`; everything else in the structure
follows from that one being right.

The FPU state is an `fxsave` image with an `xsave` area above it when the
machine has one, carrying `FP_XSTATE_MAGIC1` in the software record and
`FP_XSTATE_MAGIC2` at the end of the extended area. A consumer that knows only
`fxsave` reads the first 512 bytes and is right; one that reads the software
record learns the extended size and finds the trailing magic, which is how it
knows the area was not truncated.

## The reservation

The frame is placed 128 bytes below the interrupted stack pointer, and the
subtraction happens before any other. That is DR-0006 executed: the red zone is
honoured at the delivery site the way a kernel does it, rather than compiled
around with `-mno-red-zone` forever. A delivery onto an alternate stack skips
the reservation because it is not building on the interrupted stack at all.

The certification holds the placer to it by arithmetic rather than by
inspection — the placement reports how many bytes it skipped and where its
highest written byte is — and then holds the whole path to it by measurement: a
hand-written leaf whose accumulator lives only at `-8(%rsp)` folds correctly
across five hundred deliveries. `spin.S` is that leaf, and it is hand-written
because hand-written assembly is exactly the residue a compiler flag never
reaches. WP-16's ledger exists to bound that residue; this closes it.

## The trampoline

The sending thread does not build the frame. It cannot: a Windows stack grows
by faulting on a guard page, and that fault is a stack extension only for the
thread that owns the stack, so a write from another thread takes an unhandled
guard-page violation in the writer. The first version of this package did build
it from the sender, passed at twenty deliveries, and died at a hundred.

So the hijack copies the target's register file into a record, points the
target at `elfsysv_sig_enter` with the record in `%r11`, and resumes it; the
target steps its own stack down a page at a time, builds its own frame, and
enters the handler. That is Cygwin's shape, and it is the trampoline this work
package is named for. DR-0030.

## The return

A handler returns to `elfsysv_sig_return_tramp`, whose address the frame's
first word carries, and the trampoline calls `elfsysv_sigreturn` with the frame
address. The restore ends in a same-privilege `iretq`, which takes the
instruction pointer, the flags and the stack pointer from a frame on a stack
the runtime owns and writes nothing at the destination.

The obvious alternative is `setcontext`'s: load the stack pointer, push the
return address, `ret`. That push lands at the destination stack pointer minus
eight, which is the first word of the red zone — the word spike 3 measured
Cygwin's delivery taking every time. It is not available here.
`t/iretq_probe.c` establishes on this host that a user-mode `iretq` returns and
that it sets the stack pointer from the frame, and it gates the rest of the
run: a machine where the instruction is not usable fails at the probe.

## What is checked on the way back

Everything, from the bytes, again.

Between the placement and the return, the handler ran with a pointer to its own
return state in `%rdx`. What comes back is installed into a register file and
`iretq`'d into, so `elf_sigframe_check` re-derives every invariant rather than
trusting the ones the placer maintained. The extent is authenticated together
with the frame address, because an extent a handler can rewrite is not a bound;
every pointer is bounded before it is followed; and the FPU area must carry its
magic at both ends before it is installed, since `xrstor` with a bad state
bitmap faults rather than failing.

The authenticator and the extent live in `uc_mcontext.__reserved1`, which Linux
zeroes and no consumer reads.

That check is the package's hostile input and it is fuzzed: mutated and
truncated frames against a guard page under the undefined-behaviour sanitizer,
with every acceptance re-derived independently and every refusal required to
carry a reason. The first thing the fuzzer found was a four-byte read of the
trailing magic at an offset a handler controls, which is now required to be a
multiple of eight before it is used to address.

## What is certified

`t/run.sh` builds all of it and holds it to the done-when.

The probe first, because everything rests on it. Then the unit test: the sizes
and offsets, the placement arithmetic on both stacks, the mask and alternate
stack rules, what each `SA_` flag means, and one full round trip driven on a
stack of its own so the reserved bytes can be painted and read back — placement,
frame, handler, trampoline, check, restore, `iretq`, with the paint intact and
a marker in a callee-saved register proving the restore. Then the fuzz target.
Then the done-when itself, against a real thread through the host's real calls.

The end-to-end run carries a control arm, and this is the part worth reading.
It repeats every delivery with the reservation switched off — the construction
DR-0006 rejected, writing the word Cygwin's first instruction writes — and
requires the leaf's fold to break. A run where the control arm reports an
intact red zone fails. `spike/cygwin-from-source` did not have that arm and its
measurement was silently watching the wrong bytes; this one cannot be.

On 2026-08-30 the reserving arm kept the red zone whole over 500 deliveries,
the control arm broke it, the alternate-stack arm kept it, and the reservation's
cost came out between −23% and +16% of a delivery across runs, which is to say
below the noise of a path that costs three system calls and a thread
suspension. DR-0030 reads that against DR-0006's bands.

## What this package does not do

Decide what an uncaught signal does. `ELF_SIG_DEFAULT` comes back to the caller
and the process-level answer — terminate, dump, stop — is not here.

Defer a delivery that lands inside the host runtime. Cygwin's `interrupt_now`
defers when the target is inside `cygwin1.dll` or a Windows DLL, and this
redirects wherever the target happens to be. That is the largest untested case
and DR-0030's Not verified section carries it, along with the per-thread split
of the signal state and the `SA_RESTART` wrapper that has not been written.
