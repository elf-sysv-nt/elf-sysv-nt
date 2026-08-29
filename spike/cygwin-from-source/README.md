# Can this machine build `cygwin1.dll`, and does a reserving delivery hold?

Two questions in one spike, and the order matters. The second is the one that
was asked; the first is the one that gates it, and gates rather more besides.

## Why this exists

DR-0006 settled that the red zone is repaired at the delivery site rather than
compiled around, and left the price to WP-43. That deferral was wrong in a way
worth naming, because the record it produced could not have been read any
other way: the reservation is one `sub` instruction against a delivery that
costs microseconds, so DR-0006's cost bands can only ever return "proceed".
A criterion that has one possible answer is ceremony.

What actually gates the decision is not cost but whether the reservation
survives contact with the real path. Spike 7 measured a model built on spike
3's hijack and its own Not verified section says so: that a reserved delivery
in `sigdelayed` itself would behave as its stub did "is the hypothesis WP-43
re-measures, not a finding."

Testing that hypothesis needs a `cygwin1.dll` built from source, and nothing
in this project has ever built one. Spike 3's README is explicit that the
runtime it crossed into was "Cygwin's, unmodified, called normally; nothing was
rebuilt." So the red-zone question is standing behind a much larger unmeasured
claim, which is that phase 2 is possible at all.

`AGENTS.md` already says Cygwin binaries are backward compatible only and that
building from source is the only route. The entire runtime chain, WP-20 through
WP-25, assumes a source build works. Nobody had tried.

## The two questions

**Can a stock `cygwin1.dll` be built from the pinned source on this machine,
and does a program run against it?** This gates phase 2 entirely. A no here is
a far larger finding than anything about the red zone.

**Does a `sigdelayed` that reserves 128 bytes before it builds a handler frame
keep the red zone whole, and what does it cost against the unmodified build?**
This is spike 7's hypothesis against the real path, and it is what DR-0006's
successor is written against.

## Method

Build in the primary Cygwin root. The RHEL-8.10 emulation cannot do this job
and that is a finding rather than a preference: it is pinned at Cygwin 3.0.7
from a 2019 snapshot because RHEL 8.10 ships gcc 8 and python 3.6, and a 2019
toolchain will not build a 2025 Cygwin. Raising it would defeat the instance.
The two roots do different jobs.

`prerequisites.tsv` lists what was missing before this spike and why each is
wanted. Every line of it was absent.

1. Configure and build the tree unmodified. Record what it takes and what it
   produces.
2. Run a program against the built DLL rather than the installed one, which is
   the difference between a build that completes and a build that works.
3. Patch `sigdelayed` to reserve 128 bytes before it builds the handler frame,
   rebuild, and put spike 7's cases against the real path: a value carried
   only in the red zone across many deliveries, nesting, and an alternate
   stack.
4. Time delivery on both builds, so the cost is a number rather than an
   adjective.

Step 3 is the measurement. Steps 1 and 2 are the thing nobody had checked.

## What decides it

The build works and the reservation holds: DR-0006's direction is confirmed
against the real path, its bands are replaced by a measured cost, and a
successor record retires `-mno-red-zone` on a schedule rather than on a hope.

The build works and the reservation does not hold: the direction reopens. The
flag stops being scaffolding and becomes the answer, and WP-16's assembly
ledger becomes load-bearing rather than a precaution.

The build does not work: the red zone is the least of it. Phase 2 rests on
rebuilding this DLL, and the reason it cannot be rebuilt is the finding.

## What came back

Ran 2026-08-29. The first question is answered and the second is not.

**The build works.** `cygwin1.dll` builds from `cygwin-3.6.10` in the primary
root, once fifteen absent prerequisites are installed and `--disable-doc` is
passed. The first attempt died on `docbook2texi is required to build
documentation`, which winsup offers a switch for and which is cheaper than a
docs toolchain. That is the finding that mattered: phase 2 rests on rebuilding
this DLL and nobody had ever tried.

**The reservation patch is real.** Two lines against `scripts/gendef`, and
`sub $0x80,%rsp` appears ahead of the first push in `sigfe.o` and in the linked
DLL. `ret $128` releases it, because at that point in the epilogue every
register is restored and there is nothing left to add with.

**The measurement failed, and the differential is what proves it.** A frame
shifted down by exactly 128 bytes must move the nearest observed write down by
128. Stock reported the nearest write at 296 bytes below `%rsp`; patched
reported 264. That is a shift of −32, in the wrong direction and the size of
ordinary variation, so what the probe watches between 264 and 512 is not
`sigdelayed`'s frame and its silence between 8 and 264 is not evidence of
anything.

The control arm rules out simple blindness: a `call` placed in the same window
was caught 2000 times out of 2000, with a return address as the clobbering
value. The instrument can see a write at −8. It is not seeing this one.

## What this does not say

That Cygwin 3.6.10 preserves the red zone. That reading is available from the
numbers and is not supported by them.

That spike 3 was wrong. It measured `nearest:8` on Cygwin 3.0.7 in the RHEL
emulation; this probe cannot reproduce that shape on 3.6.10. Two probes, two
Cygwin versions, and the reason they disagree is unestablished — it may be the
seven years between them, or one of the two may be watching a stack the
delivery does not use.

That `-mno-red-zone` can come off. DR-0006 stands exactly as written, and this
spike moves nothing in it.

## Not verified

Where `sigdelayed`'s frame actually lands relative to an interrupted leaf's
`%rsp`. Establishing that wants the handler itself reporting its own frame
address rather than a watcher inferring it from damage, which is the next
probe rather than a refinement of this one.

Whether the delivery hijacks the spinning leaf at all on 3.6.10.
`interrupt_now` defers when the thread is inside the DLL or a Windows DLL, and
a deferred delivery runs at a different `%rsp` entirely. Nothing here
distinguishes the two, and it is the most likely explanation for both the
disagreement with spike 3 and the failed differential.

The cost of the reservation, which was the question DR-0006 sent to WP-43 and
which this spike never reached.

The rebuilt DLL under a Cygwin parent. It would not load from a Cygwin shell,
exit 127, because a process tree cannot hold two `cygwin1.dll` builds at once;
the measurements above ran it from `cmd`. That is a property of Cygwin rather
than of the build, and anything testing a rebuilt runtime inherits it.
