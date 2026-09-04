# Cloning a process with NtCreateProcessEx

Can `NtCreateProcessEx` with a null section handle clone the calling process's
address space on this Windows 11 build, giving us the primitive a `fork` would
rest on? `clone-probe.c` takes the measurement, `measure.sh` builds it and the
Cygwin fork timer beside it and writes `results-<date>.txt`, and the reading of
that transcript is under **The verdict** below. It ran on 2026-09-04: the clone
is created and its address space clones cleanly, and then no thread will run in
it.

## Why it matters

Proposal 0011 puts a real `fork` on the executive's clone rather than on
Cygwin's copy-and-fixup, and open question 2 is the one this answers: whether
`NtCreateProcessEx` cloning "is sound on Windows 11 24H2 with Control Flow
Guard, user-mode shadow stacks off, and the operator's endpoint product
loaded." The proposal's own "Not verified" section lists it first among the NT
behaviours it reads out of documentation rather than out of a measurement --
"that `NtCreateProcessEx` with a null section clones on Windows 11." Interix
built its POSIX `fork` on this primitive, but its source was never public, so
the precedent is an account of a design and not a thing this project can run.
Nothing here had measured it. Now something has.

## What is being asked, exactly

`NtCreateProcessEx(&child, PROCESS_ALL_ACCESS, NULL, NtCurrentProcess(),
PS_INHERIT_HANDLES, NULL /* SectionHandle */, NULL, NULL, FALSE)` is the call.
A null section handle is the documented signal to the executive that the new
process is to be a clone of the parent rather than an image loaded from a file.
The seven questions take it apart:

1. **q1, the clone is created.** The call succeeds and hands back a process
   handle. The NTSTATUS is recorded either way.
2. **q2, the address space clones.** A private, committed page written in the
   parent before the clone reads back in the child, at the same address, with
   the same contents. The probe proves it with `NtReadVirtualMemory` from the
   parent, so the answer does not lean on q5, the part that breaks. A control
   word written *after* the clone must not appear in the child; a real
   copy-on-write clone keeps the two address spaces separate from that point
   on.
3. **q3, inherited handles.** A handle marked inheritable in the parent is
   valid in the child at the same numeric value; the probe duplicates it back
   out of the child and signals through it to prove the two values name one
   object. The control is a handle left non-inheritable, which must be absent.
4. **q4, a shared section view.** A pagefile-backed `SEC_COMMIT` section, mapped
   `ViewShare` in the parent before the clone, is present in the child at the
   same address, and a write from either side is seen by the other.
5. **q5, a thread after the clone.** A thread created in the child *after* the
   clone, with `NtCreateThreadEx` and again with `RtlCreateUserThread`, runs
   and reports a result through the inherited handles. This is the part that
   historically breaks: a bare executive clone was never registered with
   `csrss`, and the first thread to run is the one that finds out.
6. **q6, mitigations.** The child's and the parent's Control Flow Guard and
   user-mode shadow-stack (CET) policies, read from the parent with
   `GetProcessMitigationPolicy`, because open question 2 names both by name.
7. **q7, the cost.** Median wall time per clone against the median of Cygwin's
   `fork` on the same machine, over the same count. A measurement that rides
   along as context; the verdict never reads it.

## Method

The probe is native, built with `x86_64-w64-mingw32-gcc`, because the design's
host process runs on `ntdll` with no Cygwin under it, so a Cygwin binary would
be measuring a different process with a different `fork` in its runtime. It
resolves the `Nt*` and `Rtl*` entry points out of `ntdll` by name and calls
them directly. The worker thread the probe tries to run in the clone touches
nothing but those resolved pointers and the inherited shared view -- no CRT, no
`kernel32`, no heap -- because an unregistered clone is exactly the process
where reaching for `csrss`-backed services would hang or fault, and the worker
is the one place that would find out the hard way.

Questions 1 through 4 and 6 run against a single clone and read it from the
parent, so a failure in q5 leaves them intact. Question 5 takes a fresh clone
per attempt, so a hang in one variant cannot be read as a result for the next,
and it runs a control first: the identical call against this process, which
must succeed, so that a refusal in the clone means something about the clone
rather than about the call.

The Cygwin `fork` comparison is a separate, tiny program, `fork-timer.c`, built
with the Cygwin `gcc`, because Cygwin's `fork` is the thing it prices and there
is no other way to call it.

## The verdict

`finding=clone-without-threads`, on `results-2026-09-04.txt`, taken on Windows
10.0.26200.9168 under Cygwin 3.6.10, `x86_64-w64-mingw32-gcc` 14.4.0, on the
host `ins-15`.

The clone is created and its address space is genuinely cloned; the thread is
where it stops.

- **q1 -- yes.** `NtCreateProcessEx` with a null section returns
  `STATUS_SUCCESS` and a live process handle. The primitive exists on this
  build.
- **q2 -- yes.** The private page written before the clone reads back in the
  child at the same address with the same bytes, through
  `NtReadVirtualMemory` from the parent. The control holds: a word written
  after the clone does not appear in the child, so the two address spaces are
  separate copies and not one shared mapping. The clone is a copy-on-write
  snapshot, which is what `fork` wants.
- **q3 -- yes.** The inheritable handle is valid in the child at the same
  value and names the same event object; the non-inheritable control handle is
  absent. `PS_INHERIT_HANDLES` carries the handle table across.
- **q4 -- yes.** The `ViewShare` section view is present in the child at the
  same address, a write from the parent is read out of the child, and a write
  into the child is read back in the parent. Shared memory survives the clone.
- **q5 -- no.** A thread created in the child after the clone does not run.
  `NtCreateThreadEx` with plain flags, and again with the DLL thread-attach
  callbacks skipped, returns `STATUS_PROCESS_IS_TERMINATING` (`0xC000010A`);
  with `THREAD_CREATE_FLAGS_INITIAL_THREAD` it returns `STATUS_BAD_INITIAL_STACK`
  (`0xC00000F5`). `RtlCreateUserThread` returns `STATUS_PROCESS_IS_TERMINATING`
  too. The identical control call against this process runs its thread and
  reports back, so the refusal belongs to the clone. And the clone is not
  actually dying: its `ExitStatus`, read the same instant, is `STATUS_PENDING`
  (`0x103`), a running process. The executive refuses a thread in a process it
  never finished bringing up -- the clone has no `csrss` registration, and the
  thread-creation path insists on one. This is the historically documented
  break, reproduced rather than recalled.
- **q6.** Both processes report Control Flow Guard disabled and user-mode
  shadow stacks disabled, on this build, with this toolchain. The mitigations
  open question 2 worried about are simply off here; nothing in the clone's
  failure is a CFG or CET rejection.
- **q7, context.** The clone costs on the order of a few hundred microseconds
  and Cygwin's `fork` on the order of several milliseconds on this machine, but
  these are not comparable primitives -- the clone hands back an address space
  with nobody in it, where Cygwin's `fork` hands back a running process with a
  re-established heap. The clone number is the cost of the part that works and
  says nothing about the cost of a `fork` built on it, which would have to
  solve q5 first. The numbers are in the transcript and decide nothing.

So the primitive clones an address space, carries handles and shared views
across, and cannot yet host a thread. The address-space half of `fork` is
proven on this build; the execution half is blocked at exactly the place the
proposal's open question 3 anticipated, the missing `csrss` registration in a
clone. What turns this from `clone-without-threads` into a usable `fork` is
whatever gives a clone a first thread the executive will start -- registering
the clone with the subsystem by hand, or creating the process by a route that
registers it and cloning the address space into it afterward, or driving the
first thread up through a path that does not consult `csrss`. None of those is
this spike; this spike is the measurement that says which problem the next one
has to solve.

## What this does not reach

Recorded here so the limits are not mistaken for findings.

The `csrss`-registration workaround. This spike measures that a plain
`NtCreateProcessEx` clone cannot start a thread; it does not attempt to fix it.
Whether `CsrClientCallServer` can be invoked to register a clone after the
fact, whether an initial thread created suspended before any `csrss`-dependent
code runs behaves differently, and whether the `RtlCloneUserProcess` path
(which wires the registration in) succeeds where the raw call fails, are all
the next spike's questions, not this one's.

Other Windows builds. The answer belongs to the running kernel, `26200.9168`
here. The proposal's open question names 23H2 and 24H2 both; this is one build,
and the script is kept so the question is cheap to ask again on another. A
result on this build says nothing about the next Windows update.

The endpoint product. Open question 2 asks about the clone "with the operator's
endpoint product loaded." This machine runs Windows Defender only, which does
not inject a DLL into the probe, so the interaction the question worries about
is not exercised here. That is spike (b)'s territory, and it is named there.

CFG and CET when they are on. Both mitigations are disabled on this build for
this binary, so the probe reads their state and does not test a clone under a
CFG-enforced or shadow-stack-enforced parent. A clone's behaviour with those
turned on is unmeasured; it would need a binary built and a process launched
with the policies forced, which is a different setup than this spike runs.

What a thread would find if it did run. Because no thread starts in the clone,
nothing here exercises whether TLS, the loader lock, the PEB, or a re-pointed
stack behave in a clone once execution begins. Those become measurable only
after q5 turns yes, and they are the reason q5 is the hinge.

## Running it

    ./measure.sh -o results-$(date +%F).txt

The options follow docopt, with the set its siblings carry: `-o/--output` (`-`
is stdout), `-n/--iterations` to size the timing loop, `-k/--keep` to leave the
built binaries beside the sources, `-q/--quiet`, `-v/--verbose` to narrate each
question on stderr, `-V/--version`, `-h/--help`. Each is also settable as
`MEASURE_<OPTION>`. Nothing is installed and no privilege is wanted; the two
binaries are built by two compilers on purpose, the native probe by the cross
compiler and the fork timer by the Cygwin `gcc`.

## Reproducing it

The verdict word and the six pass/fail readings are the finding and come back
identical; the timings, the addresses, and the header move every run. Run it on
an unloaded host: q7 is a wall-time measurement and bends under load, though the
verdict does not depend on it. Two runs on 2026-09-04 produced
`finding=clone-without-threads` and the same q1--q6 reading both times.
