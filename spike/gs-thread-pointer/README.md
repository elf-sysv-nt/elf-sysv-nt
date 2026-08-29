# A runtime-owned thread pointer through %gs

Can a thread pointer this project owns, reached through `%gs`, survive
everything that destroyed the FS base, and what does it cost to re-establish
where it does not? `gs-tp-probe.c` takes the measurement, `measure-gs-tp.sh`
builds it and writes `results-<date>.txt`, and the reading of that transcript
is under **The verdict, 2026-08-29** below. It ran on 2026-08-29: yes for three
carriers of four, and the fourth is unavailable rather than failing.

**Gates.** The TLS layer, WP-30's body, and the specs-file value WP-13 bakes
into every object it compiles. Spike 1 closed a door and named no replacement;
this is the measurement that gives the operator something to choose between.
The choice itself stays the operator's under `AGENTS.md`, and this spike is
built so that it cannot quietly make it.

## What is being asked, exactly

Spike 1 killed `%fs` at the scheduler rather than at the instruction. The base
was writable, it addressed correctly two thousand times out of two thousand,
and then Windows reloaded the segment out of a null-base descriptor the moment
the thread left its processor. `%gs` is different in kind, because that base is
not ours and never will be: NT maintains it for the TEB, the whole system
depends on it, and nothing we do can clear it.

That difference is also the trap. On Linux the thread pointer *is* the segment
base, and one instruction reaches a variable. Here the base points at a
structure Microsoft owns, so the thread pointer has to be fetched out of that
structure before anything TP-relative can happen. What we are measuring is
therefore a chain rather than a register, and a chain has a hop for each party
that can break it.

There are four ways this can come back no, and they fail at different layers.

The chain can be broken by the scheduler, exactly as `%fs` was. This is the
least likely and the first thing to check, because a `%gs` that behaved like
`%fs` would end the question in one line and everything below it would be
wasted work.

The carrier can belong to somebody else. A hardcoded `TlsSlots` index is a
constant we need and a resource `TlsAlloc` hands out, so the two uses collide
by construction rather than by accident. Endpoint protection injecting itself
into this process is a deployment reality `AGENTS.md` already records, and an
injected DLL calls `TlsAlloc` without telling anyone. An undocumented TEB field
has a worse owner still, since Microsoft can reuse it between builds and the
failure would arrive as a Windows update.

The block can read back and not address. Spike 1 drew this distinction and it
matters more here, not less: fetching a pointer through two indirections proves
the chain, and proves nothing about whether a glibc-shaped TCB works at the end
of it. The static TLS block sits *below* the thread pointer at negative
offsets, `tcbhead_t` sits at TP and above, and the self-pointer at TP+0 has to
read back as TP. A carrier that delivers a pointer but cannot support that
layout is a carrier we cannot use.

And the fourth is not a persistence question at all. A fresh thread starts with
a zeroed TEB, and spike 1 measured a fork child's base at zero, so whatever
holds the thread pointer starts life empty on both paths. Unlike the scheduler,
these are call sites, which is the entire reason this spike is worth running.
The question is whether every one of them is reachable from code we control,
and whether any TLS access can happen in the window before our hook runs.

## The carriers

Four, measured side by side in one run, because the deliverable is a table the
operator decides from rather than a candidate this spike liked.

| | Carrier | Chain | Owner of the weak hop |
|---|---|---|---|
| C1 | a fixed `TlsSlots` index | `gs:[0x1480+8k]` | `TlsAlloc`, and any injected DLL |
| C2 | the PE TLS directory | `gs:[0x58]`, then `_tls_index` | the Windows loader |
| C3 | a word in the runtime's own per-thread block | `gs:[TIB.StackBase]`, then a fixed offset | this project, after the fork |
| C4 | a named undocumented TEB field | `gs:[field]` | Microsoft, between builds |

C3 is the shape Cygwin already uses for `_my_tls`, which makes it the one with a
working precedent on this exact platform and the one that cannot be measured
honestly yet, since the runtime it depends on has not been forked. The probe
measures a stand-in: its own block, placed at the same distance below the stack
base, reached by the same chain. What that stand-in does not exercise is listed
at the bottom.

C4 is here to be dismissed with evidence instead of taste. Somebody will
propose it, the spare fields are real and roomy, and a measured row saying so
is cheaper than the argument.

## Method

Every carrier gets spike 1's twelve cases unchanged, so the two transcripts can
be read against each other:

    round trip        write, read back, address through the chain
    syscall           GetProcessTimes, entering the kernel without blocking
    yields            SwitchToThread, Sleep(0), Sleep(1)
    blocking wait     event ping-pong against a helper thread
    migration         SetThreadAffinityMask cycled over every processor
    apc               QueueUserAPC onto an alertable wait
    hijack            SuspendThread, Get/SetThreadContext, ResumeThread
    signal, sync      raise(SIGUSR1)
    signal, fault     a read through a null pointer, caught
    signal, async     pthread_kill into a thread that is spinning
    preemption        a spin loop with a burner on every processor
    load              N threads, one distinct pointer each, for D seconds

Five cases are new, and they are the ones this spike exists for.

`address` builds a real TLS block behind the carrier and reads four places
through it: the self-pointer at TP+0, the stack-guard word at TP+0x28, a
sentinel eight bytes below TP, and a second sentinel a page below. All four
under a guard, since a chain that goes between the fetch and the dereference
takes the thread with it.

`contention` asks who else wants C1. It sweeps `TlsAlloc` at process start and
records every index returned, loads a list of DLLs the box actually has, sweeps
again, and reports the lowest index still free along with any index under 64
that was consumed. This case has no pass or fail. It produces the number that
tells the operator how much room C1 has, which is a different thing from
whether C1 works.

`thread start` and `fork` measure the lifecycle. Each records what the carrier
holds at the first instruction of the new context, then re-establishes it and
counts the instructions between entry and a working thread pointer. A carrier
with no reachable hook fails here even if it passed all twelve persistence
cases, and the transcript says so in those words, because a partial pass read
as a pass is how a bad carrier gets chosen.

`cost` prices the thing. A tight loop reads one thread-local through each
carrier and reports cycles per access against three baselines: a plain global,
which is the floor; the `%fs` sequence, which is unusable and still timeable
inside one quantum; and the same source compiled `-femulated-tls`, which is what
the other option on the table would cost. Argument has carried this comparison
far enough.

## The verdict rule

Written before the run, which is the only time it is worth anything.

A carrier passes when three things hold together. Zero mismatches across all
twelve persistence cases. Correct reads at all four places in `address`. A
named, reachable hook for both `thread start` and `fork`, with the pre-hook
window measured rather than asserted. Two out of three is a fail and is
reported by name with the case that broke it.

The overall verdict is a table and not a winner. This spike reports per-carrier
rows with their costs and hazards, and it stops. Under `AGENTS.md` the TLS
model is the operator's, so a `recommended` column would be an agent taking the
decision through the back door, and there is not going to be one.

## Running it

    ./measure-gs-tp.sh -o results-$(date +%F).txt

Usage follows docopt, with the same options its sibling carries: `--seconds`,
`--threads` and `--rounds` size the run, `--keep-binary DIR` keeps the probe
when a case fails and you would rather step through it, and `--terse` prints
the summary block alone, one `key=value` per line, which is the form to quote
in a document. `--carrier NAME` measures one instead of all four, which is what
you want on a rerun that is chasing a single row.

Nothing is installed and no privilege is wanted. Run it from the pinned 2019
root, since that is where this project's code will be built, and note that the
answer belongs to the running Windows kernel rather than to Cygwin. The
transcript records the build number for that reason.

## Reproducing it

Counts belong to the machine and the minute. The `shape` line is the part that
travels: one `carrier/case:pass` or `carrier/case:fail` per pair, in order,
which is what a later rerun should diff instead of the whole file. Two runs on
2026-08-29, at 5000 rounds over 16 load threads and at 20000 over 24, produced
the same shape character for character.

## The verdict, 2026-08-29

Yes, and it is a table. `results-2026-08-29.txt`, taken on Windows
10.0.26200.9168 under the pinned root, on the same twelve-processor Ryzen 5
7530U that ran spike 1. Three carriers of four pass every case; the fourth is
unavailable under this toolchain, which is a measured row rather than a gap.

A thread pointer reached through `%gs` survives everything that destroyed the
`%fs` base. Where spike 1 got the base back as zero on the first check of every
case that left the processor, the three available carriers came back with the
pointer they were handed across 17.6 billion checks and zero failures, through
44,963,361 context switches the scheduler actually performed in the load
window. The `preemption` case is the one that says so without a call site to
argue about: 2.8 to 3.1 billion checks per carrier, spinning while a burner sat
on every processor, and not one miss. That is the exact case that killed `%fs`,
and `%gs` holds it. The difference is architectural and expected -- these
carriers are backed by TEB and stack memory the kernel never clears, rather
than by a segment base it reloads out of a null descriptor -- but expected and
measured are different things and this is the measurement.

The `address` case closes the gap spike 1 warned about between a pointer that
reads back and a pointer that addresses. Each carrier built a real glibc-shaped
block behind it and read four places through the fetched pointer under a guard:
the self-pointer at TP+0, the stack-guard word at TP+0x28, a sentinel eight
bytes below TP, and one a page below. Two thousand times out of two thousand,
all four, on every available carrier. A `tcbhead_t` can sit at the end of this
chain.

The per-carrier rows, which is where the decision lives:

- **C1, a fixed `TlsSlots` index.** Passes every case. Its hazard is not
  persistence but ownership, and the `contention` case prices it: the lowest
  free `TlsAlloc` index was 3 at process start and still 3 after loading ten
  DLLs the box carries, so nothing consumed a fixed-range slot in this run.
  That is room, not safety -- C1's hardcoded 63 is drawn from the same 64
  slots `TlsAlloc` hands out, so an injected DLL calling `TlsAlloc` is a
  collision by construction rather than by luck, and the contention number
  says how much slack there is before it bites, not that it will not.

- **C3, a word below the stack base, the shape Cygwin's `_my_tls` already
  uses.** Passes every case, and it is the carrier with a working precedent on
  this exact platform. But the probe measured a stand-in, and the stand-in
  showed its seam at `fork`: the child inherited a nonzero word
  (`0x6ffff630000`) where thread start saw a clean zero, because the location a
  page below a fresh stack base holds whatever the OS left there. The real
  `_my_tls` owns that memory and initialises it; the stand-in does not, so this
  row proves the chain and the lifecycle hooks and defers the padding constant
  and the alternate-signal-stack question to WP-2x.

- **C4, `NtTib.ArbitraryUserPointer`.** Passes every case, identically to the
  others. It is here to be dismissed with evidence, and the evidence is
  awkward: it works. The dismissal is on ownership, which this probe cannot
  measure -- Microsoft can reuse an undocumented TEB field between builds, and
  a passing persistence table on one build says nothing about the next. C4
  passing is a row, not a recommendation.

- **C2, the PE TLS directory.** Unavailable. This toolchain, Cygwin gcc 7.4.0,
  emits emulated TLS: the image carries no PE TLS directory, `_tls_index` does
  not resolve, and the loader populates no slot at `gs:[0x58]`. The weak hop the
  table assigned to the Windows loader turned up as absence rather than
  collision -- there is nothing to hang the chain on until the runtime is built
  with native TLS, which is not how Cygwin builds today. The probe reports this
  by parsing its own PE headers rather than by failing to link, so the row
  regenerates rather than breaking the run.

The cost line is the other half of the operator's table. Against a plain global
read at 2.5 cycles and the unusable `%fs` sequence at 3.8, the three carriers
cost 5.1 (C1), 5.5 (C3) and 5.6 (C4) cycles per access -- two or three cycles
over a global, for a pointer fetched through two indirections. The comparison
that matters is the other branch on the table: emulated TLS, which this same
toolchain already pays and which measured 33.7 cycles per access, six times the
carriers. Whatever the TLS model becomes, a `%gs` carrier is not the expensive
option.

Lifecycle agrees across all three. Every carrier starts the new context empty
-- zero at thread entry, empty or stale at the fork child -- and every one has
a hook this project reaches: the thread entry routine and the post-fork path,
both call sites, both re-establishing a working pointer in tens of thousands of
cycles. This is the whole reason the spike was worth running past spike 1's no:
`%fs` died to the scheduler, which has no call site, and these carriers die to
nothing and are re-established at call sites we own.

Per `AGENTS.md` the TLS model is the operator's, so this stops here. There is
no recommended carrier and no recommended column. The table says C1, C3 and C4
persist and address at a cost the emulated-TLS branch cannot touch, that C1 and
C4 carry ownership hazards this probe cannot retire, that C3 wants re-measuring
against the real `_my_tls`, and that C2 is off the table until the runtime is
built with native TLS. Which of those the runtime is built on is the decision
the spike was run to inform, not to take.

## What this cannot measure

Recorded now rather than after the run, so that nobody mistakes the limits for
findings.

C3 without the forked runtime. The probe's stand-in exercises the addressing
chain and the lifecycle hooks; it does not exercise Cygwin's real `_my_tls`,
its actual padding constant, or what happens to that block when Cygwin moves a
thread onto an alternate signal stack. Those want re-measuring inside WP-2x
once `elfsysv1.dll` exists.

Whether a carrier that survives here survives on another Windows build. Same
limit spike 1 carries, same reason, and the script is kept so the question is
cheap to ask again.

Anything about C++ exceptions or unwind data crossing a TLS access. Spike 3
left unwind unmeasured and this spike does not pick it up.
