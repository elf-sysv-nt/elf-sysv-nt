# FS base persistence

Does Windows preserve a user-written FS base across a context switch?
`results-2026-08-29.txt` is the transcript and the reading is below it.
`fs-base-probe.c` takes the measurement and `measure-fs-base.sh` builds it,
runs it, and writes the transcript; what is kept here is the means of taking
the measurement again.

**Gates.** The TLS layer, and the toolchain target definition through it. Yes
means ELF-standard `%fs`-relative TLS at native cost. No means a TLS model of
our own, which is not a fallback anyone should pick without the operator, per
`AGENTS.md`.

## What is being asked, exactly

The System V psABI puts the thread pointer in `%fs`, and every TLS access a
compiler emits reaches through it. On x86-64 Windows the TEB lives in `%gs`
instead, so `%fs` is free. Free and preserved are different claims, and only
the second one is load-bearing here.

There are three separate ways the answer could come back no, and they are not
equally bad.

The instruction may not be there at all. `WRFSBASE` is a fault unless the
kernel has set `CR4.FSGSBASE`, and no user-mode workaround exists on x86-64:
Windows offers no LDT to install a descriptor in, and no API that sets a
thread's FS base. A `#UD` here ends the question in one line.

The base may be writable and then reset. The kernel restores segment state on
its way back to user mode, and a `MOV` into `%fs` reloads the base out of the
descriptor, which is zero. Anything that puts the thread through the kernel is
therefore a candidate for silently clearing it: a reschedule, a migration to
another processor, a fault, an APC, a `SetThreadContext` from another thread.
That last one matters more than its share, because it is how Cygwin delivers a
signal.

The base may read back and not address. `RDFSBASE` returning what was written
proves the kernel kept a copy somewhere. It does not prove address translation
is using it. A base that reads back correctly and does not address is worse
than one that fails outright, because it passes the obvious check and corrupts
under the real one.

So every check the probe makes is the same shape twice over. Write a sentinel,
provoke the event, read the base back through `RDFSBASE`, and read a magic word
back through `%fs:0`. The sentinel is a mapped page rather than an invented
number, which is what makes the second half of that possible.

## Method

`fs-base-probe.c` runs a capability gate and then twelve cases. The gate is
`CPUID.(EAX=7,ECX=0):EBX[0]`, `IsProcessorFeaturePresent(PF_RDWRFSGSBASE_AVAILABLE)`,
and a guarded execution of the instruction itself under a `SIGILL` handler,
because the first two report what the processor and the loader believe rather
than what the running kernel permits.

The cases, in the order they run:

    round trip        write, read back, address through %fs:0
    yields            SwitchToThread, Sleep(0), Sleep(1)
    blocking wait     event ping-pong against a helper thread
    migration         SetThreadAffinityMask cycled over every processor
    apc               QueueUserAPC onto an alertable wait
    hijack            SuspendThread, Get/SetThreadContext, ResumeThread
    signal, sync      raise(SIGUSR1)
    signal, fault     a read through a null pointer, caught
    signal, async     pthread_kill into a thread that is spinning
    thread start      the base a freshly created thread starts life with
    fork              the base the child sees on the other side
    load              N threads, one distinct base each, for D seconds

Load is the case the milestone had in mind and it is deliberately last, since
the cheap cases name the mechanism and this one only counts. Every thread in
it holds a base no other thread holds, so a mismatch distinguishes a base that
was cleared from one that was crossed with another thread's.

Two counts come out: checks made and checks failed. The transcript also carries
the process-wide context switch count over the load window, read from
`NtQuerySystemInformation`, so that the switches claimed are the ones the
scheduler actually performed rather than the yields the probe asked for. That
number is a nicety and its absence is not a failure; a kernel that declines the
query prints `unavailable` and the case still stands on its own count.

The verdict rule is written before the run, which is the only time it is worth
anything. Yes requires every case to pass with zero mismatches. Any case that
fails is reported by name with the value that came back, because a base cleared
to zero and a base holding another thread's pointer send the design in different
directions.

## Running it

    ./measure-fs-base.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted. The probe compiles in a scratch
directory that is removed afterward; `--keep-binary DIR` keeps it instead, which
is what you want when a case fails and you would rather step through it than
read about it.

`--seconds`, `--threads` and `--rounds` size the run. The defaults are 10
seconds, twice the processor count, and 20000 rounds for the cheap cases, which
is about a minute in total. `--terse` prints the summary block alone, one
`key=value` per line, which is the form to quote in a document.

Run it from the pinned 2019 root, since that is where this project's code will
be built, and note that the answer belongs to the running Windows kernel rather
than to Cygwin. The transcript records the build number for that reason.

## Reproducing it

The counts in a transcript belong to the machine and the minute it was taken
and no rerun will match them. The `shape` line in the summary is the part that
does: one `case:pass` or `case:fail` per case, in order, which is what WP-T3
should diff rather than the whole file. Four runs on 2026-08-29, at round
counts from 5000 to 50000 and load widths of 24 and 48 threads, produced the
same shape character for character.

## The verdict, 2026-08-29

No. `results-2026-08-29.txt`, taken on Windows 10.0.26200.9168 under the
pinned root, on a twelve-processor Ryzen 5 7530U.

The mechanism is there and it works. `CPUID` advertises FSGSBASE, Windows
reports the feature, `WRFSBASE` executes rather than faulting, and a base
written by hand addresses `%fs:0` correctly two thousand times out of two
thousand. Had the answer been going to be no at the instruction, it would have
been no in one line and this would be a shorter document.

It is no at the scheduler instead. Every case that lets the thread leave its
processor gets the base back as zero, and gets it back as zero on the first
check rather than the hundredth:

    blocking wait   20000 checks   20000 failures   first at check 1
    signal, sync     4000 checks    4000 failures   first at check 1
    migration        2000 checks    1999 failures   first at check 2
    yields          20000 checks   19997 failures   first at check 4

The value that comes back is always zero and never another thread's pointer,
across the seven billion checks in the run of record. So this is the kernel
reloading `%fs` from a descriptor whose base is zero, which is what the
architecture does on a segment load, rather than anything resembling a crossed
save area. `fork` and thread creation agree: a child and a fresh thread both
start at zero regardless of what the parent held.

Two cases pass, and they are the interesting half.

    round trip       2000 checks   0 failures
    syscall         20000 checks   0 failures
    apc              4000 checks   0 failures

`syscall` calls `GetProcessTimes` twenty thousand times, which enters the
kernel and returns without blocking, and the base is intact every time. So a
kernel transition is not what clears it. Being descheduled is. That distinction
was the reason for adding the case and it closes the obvious workaround: if the
base died on the way back from a call, a runtime could re-establish it at the
call site, and there are only so many call sites. It dies to the scheduler, and
the scheduler has no call site to hook.

The `preemption` case is the one that says so without argument. Its thread
makes no system call at all; it spins on `RDFSBASE` while a burner sits on
every processor, and it loses the base anyway. In the run of record that took
13.4 million checks, which the case's own rate of 462 million a second puts at
28.9 milliseconds. Across the day's runs the figure ranged from 1.9 to 73.8
milliseconds, and the range is the machine's load rather than anything about
the mechanism. However long it is, it is one quantum on a quiet box, and one
was enough.

`apc` passing is a smaller point and worth naming so nobody reads it as
contradiction. `SleepEx(0, TRUE)` with an APC already pending runs the APC and
returns without ever waiting, so it is a kernel transition and not a
deschedule; an earlier run at settings that let it block reported 520 failures
in 800. It belongs with `syscall` rather than against it.

## What this costs

`%fs`-relative TLS at native cost is not available on this platform, which is
the branch `milestones.md` named. Per `AGENTS.md` the choice of what replaces
it is the operator's and not an agent's, so this document stops at the
measurement. What the measurement rules out, though, is narrower than it might
look and worth writing down while it is fresh:

- Re-establishing the base after each call into the runtime. Ruled out. The
  `preemption` case loses it with no call involved.
- Re-establishing it on signal delivery and at thread start only. Ruled out for
  the same reason, and `hijack` fails at check 8 besides.
- Anything that reads TLS through `%fs` in compiled code at all, which is every
  object a stock `-mtls-dialect` GCC emits for the psABI. This is the part that
  reaches the toolchain layer rather than merely adding work to it.

Not ruled out, and not measured here: whether `%gs` behaves differently, since
Windows does maintain a base there for its own TEB. That is a different
question with a different hazard, since the base is not ours to take, and it
wants its own spike rather than a paragraph in this one.

## Not verified

That the answer is a property of Windows rather than of this machine. One
kernel build, one processor, one vendor. The mechanism inferred from the zero,
a segment reload out of a null-base descriptor, is architectural and would be
surprising to find varying, but it is an inference from the value rather than a
reading of the kernel.

That `IsProcessorFeaturePresent` reporting the feature means what it appears to
mean. It agreed with the instruction here, so nothing hangs on it.

Whether a later Windows preserves the base. Nothing suggests one will, no API
exists to ask for it, and no such API was searched for beyond the documented
surface. Rerunning this on a newer build is cheap and the script is kept for
that.
