# Spike (d): hijacking a thread that is not cooperating

Can a signal be delivered asynchronously into a thread that is not cooperating
-- one spinning in a tight user-mode loop with no syscalls and no faults, or one
blocked in a kernel wait -- by suspending it and rewriting its context, on this
Windows 11 build? Proposal 0011 delivers signals exactly this way, and nothing
this project has run had ever measured it here. The answer this spike records is
yes for a thread in user mode: it is suspended, its `%rip` and `%rsp` are
rewritten to a handler frame laid 128 bytes below the interrupted stack pointer,
the handler runs, and the thread returns cleanly to its loop from the saved
context. For a thread parked inside a kernel wait the rewrite does not fire while
the wait is pending; it takes effect only once the wait returns -- which is not a
failure but the behaviour the design counts on. `results-2026-09-04.txt` is the
transcript, and the reading is below it.

## Why it matters

Proposal 0011's signal section (§ 7) delivers a signal to a thread running user
code with nothing pending by interrupting it, and names the mechanism the
substrate's `thread_interrupt`: under the N kernel, `NtSuspendThread`,
`NtGetContextThread`, a check that the thread is in user code rather than inside
the kernel, a rewrite of the context so the thread resumes in the signal
trampoline with the frame built, then `NtResumeThread`. If the thread is inside
the kernel the signal is left pending for the gate's exit path to deliver. That
in-kernel check is a per-thread flag the gate sets on entry and clears on exit,
the analogue of the kernel/user distinction a real kernel reads from the CPU.
Phase 0's spike (d) is named for measuring this on Windows 11, with the flag
honoured and the latency priced, before the design commits to it.

The frame's placement is not free-floating. DR-0030 fixed the shape of an ELF
delivery on this platform: the receiving thread builds the frame, and the frame
is placed 128 bytes below the interrupted stack pointer before anything else is
subtracted. DR-0050 retired `-mno-red-zone` and moved the red-zone repair to the
delivery site, so a compiled leaf keeps its 128 bytes across a signal the way it
would on Linux. A hijack that respected neither would deliver onto the red zone
and corrupt whatever scratch a leaf had parked there. So the question is not only
whether the hijack lands, but whether it lands below the reserved region and
leaves it whole.

This is the NT-side sibling of `spike/redzone-delivery`. That spike drove the
same self-hijack to price the reservation itself -- what a delivery does to the
frame once it has arrived -- and found that a frame 128 below `%rsp` keeps the
red zone intact while a frame at the interrupted `%rsp` destroys it at offset 8.
This spike asks the half that one set aside: whether the delivery reaches a
thread that is not asking for it, spinning or blocked, and whether the in-kernel
flag the design leans on actually describes where a suspended thread stopped.

## Method

The probe is native `mingw` -- `hijack.c` with `hijack.S` -- not Cygwin, because
the delivery under test is the NT one. Cygwin's `gcc` would drag its own signal
runtime and its own idea of a thread into the measurement; a `ntdll`-facing probe
keeps the thing measured close to what the N kernel would do. The suspend, the
context read and write, the resume, the wait and the APC are all the `Nt`
entrypoints, resolved from `ntdll` by name; thread creation and the mitigation
query are ordinary Win32, incidental scaffolding rather than the thing under
test.

Two targets are hand asm, and both are leaves that make no call, for the reason
spike 3 spelled out and DR-0030 inherited: the moment a watcher calls anything
the return address lands eight bytes below `%rsp`, inside the red zone it came to
watch, and the watcher has destroyed its own evidence. `spin_target` paints a
distance-encoded pattern into 1024 bytes below its stack pointer, sets a running
flag, and reads the pattern back forever, counting what moved and repainting it;
it issues no syscall, so it is a thread spinning purely in user mode, and
anything below `%rsp` that changes came from the delivery. `flag_target` raises
an in-kernel flag around a critical region bounded by two exported label
addresses, lowers it after, and spins an equal while with the flag down, so the
driver can ask whether a `%rip` it caught after suspending agrees with the flag
the thread left behind.

The deliverer plays the kernel on both sides, as the redzone sibling's did. It
suspends the target, saves the whole context, points `%rip` at `deliver_stub` and
`%rsp` at the chosen frame -- the interrupted `%rsp` for the naive control, 128
below it for the reserved delivery -- and resumes. The stub stamps `rdtsc` as its
first instruction so the delivery can be timed to the handler's first
instruction, marks the word at its own `%rsp-8` the way `sigdelayed` does, gives
the C handler its shadow space and calls it, then spins until the driver lifts it
out by restoring the saved context, which is this model's `sigreturn`. Registers
come back exact; what the delivery wrote below `%rsp` does not, which is what the
leaf watcher is there to read.

## The seven questions

The probe answers seven `key=value` lines. Two carry controls the rest lean on:
the naive delivery proves the leaf watcher is not simply blind, and the alertable
APC proves the APC queue works before q6 reports that it stays silent.

| key | asks |
|---|---|
| `q1_spinning_user_thread` | a thread in a tight user loop is suspended, its context rewritten to the handler, and it returns cleanly to the loop; the handler ran on every delivery |
| `q2_frame_below_redzone` | the frame laid 128 below `%rsp` leaves the red zone whole -- the pattern the loop keeps at offsets 8 through 128 survives, nearest write past it; the naive control loses offset 8, so the watcher can see |
| `q3_thread_in_syscall` | a thread blocked in `NtWaitForSingleObject` reports a user `%rip` at the syscall's return inside `ntdll`, and the rewrite takes effect only once the wait returns, not while it is pending |
| `q4_in_kernel_flag` | the flag observed after `NtSuspendThread` reliably describes where the thread stopped; over twenty thousand suspensions no delivery decided on a lowered flag caught the thread inside the guarded region |
| `q5_cet_shadow_stack` | the `%rip` rewrite holds against this build's shadow-stack configuration, with the mitigation state read from `GetProcessMitigationPolicy` reported beside the result |
| `q6_apc_alternative` | a user APC queued at the spinning thread never runs, since the thread never waits alertably -- the alternative the design rejects, measured rather than recalled |
| `q7_latency_ns` | median and 99th-percentile latency from the decision to deliver to the handler's first instruction, over two thousand deliveries; a measurement, not a finding |

## The verdict, 2026-09-04

`finding=hijack-delivers-user-defers-kernel`, `verdict=yes`.

**A spinning user thread is hijacked and returns clean.** Two thousand
deliveries, each caught the thread in its loop, redirected `%rip` to the handler,
and restored the saved context; the handler ran and returned every time, and the
thread went on scanning its region afterward, its loop counter still climbing.
This is the case § 7 calls the fourth and hardest, a thread running user code
with nothing pending, and it lands.

**The frame respects the red zone.** The reserved delivery, 128 below `%rsp`,
never touched the pattern the leaf kept at offsets 8 through 128; the nearest
word it reached was offset 136, the stub's own `%rsp-8` one word past the
reservation, on every delivery. The naive control lost the word at offset 8, the
way the real `sigdelayed` does, which is what makes a clean reserved run mean the
reservation worked rather than the watcher having gone blind. This is DR-0030 and
DR-0050's shape, confirmed from the delivering side.

**A thread in a kernel wait defers, and the flag is trustworthy.** A thread
blocked in `NtWaitForSingleObject` reported a user-mode `%rip` at the wait's
return inside `ntdll`. Rewriting its context and resuming did nothing while the
wait was pending; the handler ran only after the event was signalled and the wait
returned. That is precisely why the design does not hijack an in-kernel thread
but leaves the signal for the gate's exit path. And the in-kernel flag earns its
place: over twenty thousand suspensions, every time the flag was down the caught
`%rip` was outside the guarded region, so no delivery would ever have landed
mid-kernel. Zero violations.

**The APC alternative stays silent, and shadow stacks are off.** Two thousand
user APCs queued at the spinning thread never ran, because it never waits
alertably; the same APC at a thread that does wait alertably drained at once, so
the silence is the thread's, not a broken queue. The process runs with user-mode
shadow stacks disabled, which is what a `mingw` binary without the CET opt-in
gets, so the `%rip` rewrite is unconstrained here -- a finding true for this
configuration and bounded by it.

The latency is context, not a verdict: a delivery took a median near twenty
microseconds from the decision to the handler's first instruction, dominated by
the suspend, the context write, the resume, and the scheduler putting the target
back on a core. It moves between runs and between machines and is not part of the
finding.

## What this does not reach

The real trampoline. This drives the delivery with a self-hijack and a stub, not
the ELF `rt_sigframe` with its `ucontext`, `siginfo` and saved FPU state, and not
glibc's `__restore_rt` and `rt_sigreturn` for the return. The 128 reserved here is
a bare gap, not a gap around a real frame layout, so offset 136 is a property of
this stub -- 128 plus its own first word -- and not the offset a real path would
report. WP-43's neighbour owns that integration and re-measures it.

The waiting thread's real resume. q3 measures when the rewrite fires, not a
faithful `sigreturn` out of a syscall. After the wait released, the teardown
restores the saved context so the thread can unwind and exit; it does not model
what a real kernel would do to resume an interrupted, restartable syscall. That
the rewrite defers is the finding; the clean-resume path is not.

The flag under real mutation. q4 observes the flag and the caught `%rip` and
classifies the deliver-or-defer decision, rather than actually rewriting the flag
target on the decided deliveries, so the model target stays intact across twenty
thousand samples. The dangerous case -- flag down while `%rip` is inside the
region -- is exactly what the observation captures, so the reliability finding is
whole; what it does not exercise is a real delivery racing a real flag store, which
belongs to the runtime's own tests.

Shadow stacks on. The `%rip` rewrite is measured with user-mode shadow stacks
off. A process built with CET would have the kernel validate a `SetThreadContext`
instruction pointer against the shadow stack, and whether the delivery survives
that is a different measurement this spike does not take; it reports the
mitigation state so the bound is visible rather than assumed.

The clock. Latency is `rdtsc` deltas converted with a QPC-calibrated frequency,
which assumes an invariant TSC and a delta that stays meaningful when the deliverer
and the target sit on different cores. It is fine for an order-of-magnitude
reading and is offered as nothing more.

One Windows build, one processor count, one compiler, as with every spike here.
Spike 1 decided a layer off one machine, so the narrowness is worth stating rather
than assuming away.
