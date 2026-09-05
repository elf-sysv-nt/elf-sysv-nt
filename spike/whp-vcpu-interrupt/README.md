# Spike (g): forcing a virtual processor out of its run

Under substrate H, can one host thread reach into a virtual processor that is
spinning in guest code and force it back to the host, the way a kernel drags a
CPU into itself to deliver a signal? And once it is out, is it still the same
vCPU, resumable where it stopped, or has the cancel torn it down? The answer
this spike records is yes on both counts, and yes to the harder form as well:
the host can inject a handler by rewriting the guest's registers on the way out,
run it, and put the original loop back. `results-2026-09-05.txt` is the
transcript; `finding=interrupt-delivers-resumable`.

## Why it matters

Proposal 0011 puts one core over two substrates and refuses to choose between
them until the numbers are in. Section 3 draws the line between them as an
interface of nine calls, the whole of what the core asks a substrate to do, and
one of the nine is `thread_interrupt(tid)`: "force it into the kernel, soon."
Under substrate N that call is spike (d)'s hijack, `NtSuspendThread` followed by
a context rewrite. Under substrate H, section 7 says it is one function,
`WHvCancelRunVirtualProcessor`, "the vCPU's exit is the moment."

The two-substrate design rests on a claim it never tested: that the nine-call
interface is genuinely two-implementable, that each call has an honest H
realisation and not just an N one with an H sketch beside it. Most of the nine
already had a measurement standing behind them by the end of phase 0. This one
did not. Spike (f) built the partition, ran ring-three guest code, priced an
exit at roughly five microseconds, and then said so plainly in its own closing
section: `WHvCancelRunVirtualProcessor` exists, this spike never calls it, and
whether asynchronous interruption of a running vCPU costs what a normal exit
costs matters to signals under H. That sentence is this spike's charter. It is
the least-proven of the nine on the H side, and the H twin of the call spike (d)
already characterised on the N side, so it is measured here in terms that line
up against (d)'s.

**Gates.** The two-implementable premise under proposal 0011 section 3, and
through it the honesty of open question 1's substrate choice.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted, the same as spike (f): the
partition is created and driven from an ordinary user process. A full run takes
well under a second. `-l N` sets how many cancels of a running vCPU are timed,
two thousand by default; `-r N` sets how many are raced against the guest loop,
ten thousand by default. Take the timing run on an idle host, since the median
holds under load and the tail does not.

## Method

The guest is spike (f)'s, reused rather than rebuilt: one host allocation mapped
as guest physical memory, identity page tables over the first sixteen megabytes,
a GDT laid out for `STAR`, a TSS, long mode entered at CPL 3 in a single
`WHvSetVirtualProcessorRegisters` call. What this spike adds is a guest that
does not cooperate. A tight `inc %rax; jmp` loop never halts, never faults, and
issues no syscall, so nothing but an outside force takes it off the processor;
`%rax` climbs while it runs, which is how the host proves afterwards that the
guest resumed where it stopped rather than being rebuilt from zero.

One NT thread owns `WHvRunVirtualProcessor`, the shape section 3 specifies for
substrate H, one virtual processor per Linux thread each driven by one host
thread that loops on the run call. The main thread plays the kernel: it reads
and writes the vCPU's registers while the runner is idle, and it calls
`WHvCancelRunVirtualProcessor` while the runner is inside a run. A small
handshake keeps those two facts from ever overlapping, since a register write
during a run is not allowed.

One property of WHP shaped the measurement and is worth stating, because it is
the same fact spike (f) priced from the other side. A VM entry is not free; it
costs a few microseconds, and a cancel that lands inside the entry takes the
guest out at its first instruction, before it has run at all. The questions that
mean to interrupt running guest code therefore spin past the entry window before
they cancel, and the delivery question polls a word the injected stub stores
into shared memory, so that what it confirms is a stub that ran and not merely a
`%rip` that was set.

## The six questions

The probe answers six `key=value` lines, in the order the transcript reads them.

| key | asks |
|---|---|
| `q1_cancel_returns_running_vcpu` | a vCPU looping in guest code that never exits on its own is forced out by a cancel from a second thread, and the run returns with `WHvRunVpExitReasonCanceled` |
| `q2_latency_ns` | median and p99 from the cancelling thread's call to the vCPU thread seeing the return, over two thousand cancels; a measurement, not a finding |
| `q3_resumable` | after a cancel the vCPU is re-entered with no rewrite and the guest carries on, `%rax` climbed and `%rip` still inside the loop, which is what makes it an interrupt and not a teardown |
| `q4_deliver_then_resume` | on the cancel the host points `%rip` at a guest stub and `%rsp` 128 below the interrupted value, the stub runs and stores a known word, then the saved context is restored and the original loop resumes |
| `q5_cancel_before_run` | a cancel issued while the vCPU is not in a run: is it latched, so the next run returns Canceled at once, or lost |
| `q6_pending_vs_delivered` | over ten thousand cancels raced against the loop, does every cancel produce exactly one Canceled exit, none lost and none of another kind |

## The verdict, 2026-09-05

`finding=interrupt-delivers-resumable`, `verdict=yes`, every question a pass.

**A running vCPU is forced out, and the exit is Canceled.** Two hundred cancels
of a guest loop that never leaves on its own, each returned
`WHvRunVpExitReasonCanceled` with the guest still at CPL 3, none other and none
failed. This is the bare fact the substrate call rests on, and it holds.

**The interrupt is resumable, which is the whole point.** After a cancel the
same vCPU was re-entered with not one register touched, and the guest went on
counting from where it had stopped; `%rax` had climbed between the two exits and
`%rip` sat inside the two-instruction loop both times. A cancel that could only
tear the vCPU down would be a stop, not an interrupt, and would be useless to a
signal that has to hand control back afterward. This one hands it back.

**Delivery by register injection works, the way context rewrite works on N.**
On the cancel the host rewrote `%rip` to a guest stub and `%rsp` to 128 below
the interrupted pointer, the red-zone gap DR-0030 fixed for the N side; the stub
ran and stored its marker where the host read it back, the adjusted stack
pointer held, and then the saved context went back in and the original loop
resumed and climbed again. That is spike (d)'s suspend-and-rewrite, expressed in
the one place H puts user registers, and it is the evidence that H's
`thread_interrupt` is not a weaker cousin of N's but the same move on different
hardware.

**A cancel that beats the run is latched, not lost.** Issued while the vCPU was
provably not in a run, every one of two hundred cancels made the next run return
Canceled at once; none was dropped. This is the concern spike (d) handled by
hand on the N side with an in-kernel flag the gate sets and clears. WHP keeps
the equivalent state itself, so the race between a cancel and the run it means to
stop is closed by the platform rather than by the core.

**Every cancel is exactly one exit.** Ten thousand cancels fired the instant a
run was asked for, landing sometimes before the entry and sometimes inside the
guest, produced ten thousand Canceled exits, none lost to a hang and none
returning some other reason. One cancel, one exit, no leak into the next run.

The latency is context, not a verdict. A cancel took a median near four
microseconds from the call to the vCPU thread seeing the return, with a
ninety-ninth percentile in the mid teens. Read against its two siblings, that is
the useful shape: it sits right on spike (f)'s five-microsecond exit, and it is
several times cheaper than spike (d)'s NT hijack, whose median ran near eighteen
microseconds because it pays for a suspend, a context write, a resume, and the
scheduler putting a thread back on a core. Forcing a vCPU out of a run is about
as expensive as the run's own exit, and no more; the number moves between runs
and machines and is not part of the finding.

## What this does not reach

The real kernel entry. This is a bare `inc %rax` loop, not section 3's ring-0
shim with its IDT and its per-vCPU spill page, and the "handler" is a stub that
stores one word, not Linux's `rt_sigframe` with its `ucontext`, `siginfo`, saved
FPU state, and a return through `__restore_rt`. The 128 subtracted from `%rsp`
here is a bare red-zone gap, not a gap around a real frame, so it proves the
injection is possible and page-honest, not that a whole frame fits the way
WP-43's neighbour will have to show for N.

The kernel-or-user distinction. On N the interrupt is safe only because a
per-thread flag says whether the thread is in the kernel, and spike (d) measured
that flag's reliability. On H that distinction is the ring-0 shim's to make from
where the exit came from, not the cancel's, so this spike does not measure it;
it measures the cancel beneath it. The `progress-max` of zero in q5, every
latched cancel delivering before a single guest instruction, is a fact of this
host's entry path and should be read as one, not as a guarantee.

One vCPU. Spike (f) already named the multi-vCPU question as the first thing to
measure next, and it is still open: whether a cancel's cost and its one-to-one
exit hold when several virtual processors are exiting into one host process at
once is unmeasured here, and substrate H wants one vCPU per Linux thread.

The clock, and the machine. The latency is `QueryPerformanceCounter` deltas
across two threads, fine for an order-of-magnitude reading and offered as
nothing more. And it is one idle host, one AMD processor, one Windows build,
`10.0.26200`, as with every spike here; it says nothing about Intel, about a
contended hypervisor, or about a nested one.
