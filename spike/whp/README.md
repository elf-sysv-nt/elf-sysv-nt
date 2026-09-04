# WHP as a substrate

Can this machine run a Linux thread inside a Windows Hypervisor Platform
partition — long mode, ring three, a `syscall` that comes back to the host, a
page fault the host can see — and what does one exit cost?

The answer is yes on every count, and the cost is a few microseconds.
`results-2026-09-04.txt` is the transcript; `finding=whp-usable`.

## Why it matters

Proposal 0011 designs two substrates under one core and does not choose between
them. Its **open question 1** makes the choice mechanical:

> If a syscall under H costs under 10 µs and the operator accepts the
> hypervisor feature as a prerequisite, H is the better kernel emulation by
> every measure but syscall latency, and it removes the toolchain and rebuild
> projects entirely; if either condition fails, N.

Section 3 of that proposal prices a WHP exit at five to fifteen microseconds,
and its **Not verified** section is honest about where the figure came from:
the timings quoted there are "in order, a target, a recollection of published
gVisor and WHP measurements, and a measurement from `spike/gs-thread-pointer/`.
Only the last is this project's." This spike turns the middle one into a
measurement of this project's own, on the operator's machine. Phase 0's spike
(f) is what section 18 asks for, and this is it.

The second condition — whether the operator accepts the hypervisor feature as
a prerequisite — is not a measurement and this spike does not touch it.

**Gates.** Open question 1, and through it the substrate choice that phases 1
onward are built against.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted, which is the point: whether
the Windows Hypervisor Platform optional feature is on cannot be asked of DISM
without elevation, but `WHvGetCapability(WHvCapabilityCodeHypervisorPresent)`
answers it from an ordinary user process. If the answer had been no, the
finding would have been `hypervisor-absent` or, one step further in,
`whp-feature-disabled`; turning the feature on is an elevated, reboot-requiring
operator action, outside what a spike may do.

`-n N` sets how many exits are timed per mechanism, 20000 by default. A full
run is a few seconds. Take the timing run on an idle host: the median is stable
under load, the ninety-ninth percentile is not.

## Method

`whp-probe.c` builds a guest by hand rather than assembling one. Four
megabytes of host memory become guest physical memory at GPA 0 through
`WHvMapGpaRange`, and into it go identity page tables covering the first
sixteen megabytes with two-megabyte pages, a GDT laid out where `STAR` wants
it (ring-0 code at 0x08, ring-3 data at 0x23, ring-3 long code at 0x2b), a
64-bit TSS, and a few dozen bytes of machine code. One
`WHvSetVirtualProcessorRegisters` call puts the processor in long mode at CPL
3: WHP takes segment registers as cached descriptors, so entering ring three
needs no `iret` and no bootstrap in the guest at all.

There is deliberately no IDT. Anything that faults where the spike did not
intend it triple-faults, and the host sees
`WHvRunVpExitReasonUnrecoverableException` — a loud answer rather than a
handler quietly absorbing a mistake. That choice found the one real surprise
here: `hlt` is privileged, so the first ring-3 sequence, written to halt when
it was done, triple-faulted instead. Ring three leaves through a memory access
the partition cannot satisfy, or through `syscall`. Nothing else.

The syscall path is the mechanism the whole substrate rests on, and it is four
bytes of guest code. `IA32_EFER.SCE` is set, `LSTAR` points at a shim that
executes `hlt` and then `sysretq`. A ring-3 `syscall` therefore arrives at the
host as `WHvRunVpExitReasonX64Halt` with `%rcx` carrying the user return
address the instruction pushed there and CS switched to the ring-0 selector
`STAR` names; the next `WHvRunVirtualProcessor` call runs the `sysretq` that
sends the guest back. Because the guest jumps back to its own `syscall`, that
whole path loops, and one run call is exactly one syscall round trip.

Two latencies come out of this. The first is the cheapest exit the partition
can be made to take repeatedly, a ring-0 `hlt` with a jump back to it, which
prices the exit alone. The second is the ring-3 syscall round trip above,
which is what open question 1 is actually asking about. That they land within
noise of each other is the useful part: the syscall path costs what an exit
costs, and the shim adds nothing measurable.

For the comparison baseline, `NtQuerySystemTime` turns out to price a memory
load rather than a ring transition — modern Windows serves it from the shared
user data page — so the probe also times `NtQueryInformationProcess`, which
has to trap. Both are in the transcript. Per-call `QueryPerformanceCounter`
readings are coarser than either, so the native numbers are median batch
means over 500-call batches; the WHP numbers are per-exit, where a 100 ns
counter tick is a rounding error against an exit of several microseconds.

## What this does not reach

The guest here is a few dozen bytes with no interrupt handling, no APIC, no
second virtual processor, and no `WHvEmulatorTryIoEmulation` behind the memory
exits it takes. Section 3's ring-0 shim is a few hundred instructions that
owns an IDT and spills register state to a per-vCPU page; this proves the shape
of the mechanism, not that the shim is writable in that budget.

Nothing multi-threaded is measured. One vCPU on one NT thread is the easy
case, and substrate H wants one vCPU per Linux thread, all of them exiting into
the same host process. Whether the exit cost holds when eight vCPUs are exiting
at once — whether the number here is per-exit or per-host — is unmeasured and
is the first thing to measure next.

The exit measured is a halt, chosen because it repeats without host
bookkeeping. A real syscall exit under H does more: read registers out, write
registers back, and touch guest memory. Those calls are not in this number.
Nor is any kernel work. Read the figure as the floor a syscall under H sits
on, not as what one will cost.

`as_clone`, the fork primitive, is untouched. So is `thread_interrupt` —
`WHvCancelRunVirtualProcessor` exists and this spike never calls it, and
whether asynchronous interruption of a running vCPU costs what a normal exit
costs matters to signals under H.

The measurement was taken on an idle host, on one machine, one AMD processor,
one Windows build. It says nothing about Intel, about a machine with other
hypervisor-rooted features contending, or about what happens under a nested
hypervisor.
