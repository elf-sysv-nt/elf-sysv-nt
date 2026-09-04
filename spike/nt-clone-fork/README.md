# Cloning a process for fork on Windows 11

Which primitive can a real `fork` rest on here? `clone-probe.c` takes the
measurement, `native-clone.c` supplies the one parent shape the probe cannot be
itself, `measure.sh` builds all three binaries and writes `results-<date>.txt`,
and the reading of that transcript is under **The verdict** below. It ran on
2026-09-04: one of the candidates forks a child that comes back running, and
that settles the question the earlier run left open.

## Why this was measured again

An earlier pass through this spike measured exactly one candidate. It took
`NtCreateProcessEx` with a null section, found that the address space clones
cleanly, found that no thread would start in the clone, and recorded
`clone-without-threads` on that basis. The reading was sound as far as it went.
It did not go far enough: `fork`-on-NT has more than one candidate primitive,
and the decision ladder this project runs on narrows an empirical tier only once
every surviving candidate has been measured, not the first. Measuring one and
adopting its result is, in the ladder's own words, a guess with a transcript. So
this run measures the rest.

The earlier questions keep their numbers. `q1` through `q7` are the
`NtCreateProcessEx` path exactly as before, so that reading stays legible
against the new one; the candidates added here are `q8` through `q11`.

## The candidates

- **q8, `RtlCloneUserProcess`.** ntdll's fork-shaped clone, the one that returns
  a second time in the child with the calling thread cloned alongside the
  address space, the way `fork` does. The child does not need a thread created
  for it after the fact; it arrives already running one.
- **q9, `NtCreateUserProcess` with inherit-from-parent.** The bare syscall that
  produces a process from a parent's address space rather than from an image
  file, called directly rather than through a wrapper.
- **q10, the `NtCreateProcessEx` path again, one variation at a time.** The
  first thread created suspended and then resumed, which is the documented shape
  for the very first thread a process has; and the clone's `ExitStatus` read at
  the same instant, to show whether it is really terminating.
- **q11, the whole clone from a native, csrss-free parent.** A Win32 process
  carries a csrss registration; a native-subsystem process, whose ntdll startup
  never connects to the Win32 subsystem, does not, and proposal 0011 says the
  fork host must not. This was named the single most likely explanation of the
  `NtCreateProcessEx` failure, so it is tested directly: the same clone, run
  from a parent that has no csrss.

## The verdict

`finding=clone-works-via-rtlclone`, on `results-2026-09-04.txt`, taken on
Windows 10.0.26200.9168 under Cygwin 3.6.10, `x86_64-w64-mingw32-gcc` 14.4.0, on
the host `ins-15`.

`RtlCloneUserProcess` is the primitive. It forks, and the child runs.

- **q8 — yes, and this is the finding.** `RtlCloneUserProcess` with
  `INHERIT_HANDLES` returns `STATUS_SUCCESS` in the parent and
  `STATUS_PROCESS_CLONED` in the child, and the child reaches its own code on a
  live thread. It reads its cloned private page and finds the pre-clone pattern,
  so the address space came across (q2, asked again and answered from inside the
  child this time). It sets an event the parent marked inheritable, and the
  parent's wait returns, so the inherited handle names the same object (q3
  again). It writes the shared section view and the parent reads the write back
  (q4 again). All of this happens from an ordinary Win32 parent; the child does
  not need a csrss-free parent to run. This is the whole address-space-plus-a-
  thread that `fork` wants, on this build, from ntdll.
- **q5, q10 — no, under every variation.** The raw `NtCreateProcessEx` clone
  still refuses a thread. Plain flags and the DLL-attach-skipped flag return
  `STATUS_PROCESS_IS_TERMINATING` (`0xC000010A`); the initial-thread flag returns
  `STATUS_BAD_INITIAL_STACK` (`0xC00000F5`); creating the thread suspended and
  resuming it fails at creation with `0xC000010A`, before there is anything to
  resume; `RtlCreateUserThread` returns `0xC000010A` as well. The clone is not
  actually dying while it refuses: its `ExitStatus` reads `STATUS_PENDING`
  (`0x103`), a running process. The path is thoroughly dead, not dead by one
  accident of flags.
- **q11 — the parent's shape does not matter.** Run from the native,
  csrss-free parent, the identical `NtCreateProcessEx` clone refuses its thread
  with the identical `0xC000010A`. This is the sharp negative the assignment
  asked for: the most likely explanation of the failure, tested and disproven.
  Whatever the raw path is missing, it is not the parent's csrss registration.
  The native image itself launches cleanly, through `NtCreateUserProcess`,
  because `CreateProcess` refuses a native-subsystem image outright; the launch
  is not what fails, the clone's thread is.
- **q9 — the bare syscall rejects a minimal call.** `NtCreateUserProcess` with
  inherit-from-parent and no further setup returns `STATUS_INVALID_PARAMETER`
  (`0xC000000D`). The mechanism is real, but it is reachable only through the
  packaging `RtlCloneUserProcess` supplies; the wrapper uses exactly this
  syscall underneath, and the wrapper is the supported door onto it. That is why
  q8 is the finding and q9 is not a second one.
- **q6.** Both processes report Control Flow Guard disabled and user-mode shadow
  stacks disabled, so nothing in any refusal here is a CFG or CET rejection.
- **q7, context.** The `NtCreateProcessEx` clone costs a few hundred
  microseconds and `RtlCloneUserProcess` costs several milliseconds, against
  Cygwin's `fork` at roughly ten milliseconds on this machine. These are not
  like-for-like: the `NtCreateProcessEx` number prices an address space with
  nobody in it, while `RtlCloneUserProcess` and Cygwin's `fork` both price a
  running child. The numbers ride along and decide nothing.

So proposal 0011's open question 2, whether the executive's clone is sound as a
`fork` base on this build, is answered yes, through `RtlCloneUserProcess`. The
proposal's first "Not verified" item can now cite a measurement rather than
Interix's unpublished precedent. The raw `NtCreateProcessEx` clone remains what
the earlier run found it to be, an address space with no way to start a thread,
and the reason is not the one that looked most likely; but the design does not
need that path, because the fork-shaped wrapper over `NtCreateUserProcess`
carries a live thread across on its own.

## Method

The probe is native, built with `x86_64-w64-mingw32-gcc`, because the design's
host runs on ntdll with no Cygwin beneath it. It resolves the `Nt*` and `Rtl*`
entry points out of ntdll by name and calls them directly. For q8 the child
touches nothing but inherited handles and the shared view before it stops
itself, since a freshly cloned process is exactly where reaching for a
csrss-backed service would hang.

`native-clone.c` is the q11 parent: an ntdll-only native-subsystem image, built
the way sibling spike (b) `native-host` established, with `-nostdlib
-nodefaultlibs -e NtProcessStartup -Wl,--subsystem,native -lntdll`. It attempts
the `NtCreateProcessEx` clone and the thread from inside a process that has no
csrss, and it reports the one thing a launcher can read back cleanly, its exit
code, which carries the thread-creation status. `clone-probe.c` launches it
through `NtCreateUserProcess`, the same syscall q9 exercises and the only way to
start a native image, since `CreateProcess` rejects one.

The Cygwin `fork` comparison is a separate, tiny program, `fork-timer.c`, built
with the Cygwin `gcc`, because Cygwin's `fork` is the thing it prices and there
is no other way to call it.

Each thread-in-the-clone attempt takes a fresh clone, so a hang in one variant
cannot be read as a result for the next, and each runs a control first: the
identical call against this process, which must succeed, so that a refusal in
the clone means something about the clone rather than about the call.

## What this does not reach

Recorded so the limits are not mistaken for findings.

The full `fork`. q8 proves a clone comes back running with the address space,
the inherited handles, and a shared view intact. It does not exercise what a
forked child's runtime must still repair before it is a POSIX child: the heap
and loader lock inherited mid-flight, TLS, the signal state, the process
parameters. Those become the next spike's questions now that a live thread in a
clone is a thing this project can produce.

Other Windows builds. The answer belongs to the running kernel, `26200.9168`
here. Proposal 0011's open question names 23H2 and 24H2 both; this is one build,
and the script is kept so the question is cheap to ask again on the next.

The endpoint product. This machine runs Windows Defender only, which injects no
DLL into the probe, so the interaction open question 2 worries about is not
exercised here. That is spike (b)'s territory.

CFG and CET when they are on. Both are disabled on this build for these
binaries, so the probe reads their state and does not test a clone under an
enforced policy. A clone's behaviour with either turned on is unmeasured.

## Running it

    ./measure.sh -o results-$(date +%F).txt

The options follow docopt, with the set its siblings carry: `-o/--output` (`-`
is stdout), `-n/--iterations` to size the timing loops, `-k/--keep` to leave the
built binaries beside the sources, `-q/--quiet`, `-v/--verbose` to narrate each
question on stderr, `-V/--version`, `-h/--help`. Each is also settable as
`MEASURE_<OPTION>`. Nothing is installed and no privilege is wanted; the three
native binaries and the one Cygwin binary are built by two compilers on purpose.

## Reproducing it

The verdict word and the per-question readings are the finding and come back
identical; the timings, the addresses, and the header move every run. Run it on
an unloaded host: q7 and q8's median are wall-time measurements and bend under
load, though the verdict does not depend on either. Two runs on 2026-09-04
produced `finding=clone-works-via-rtlclone` and the same q1–q11 reading both
times.
