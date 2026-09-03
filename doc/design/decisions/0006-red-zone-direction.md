# DR-0006 — the red zone is repaired at the delivery site, not compiled around

Status: accepted in direction; the price is WP-43's and is not settled here
Date: 2026-08-29
Deciding: the operator
Proposal: none; taken against `spike/redzone-delivery/results-2026-08-29.txt`

## What was decided

The platform honours the red zone. Cygwin's signal delivery is repaired to
reserve the psABI's 128 bytes before it builds a handler frame, the way a
kernel does, and `-mno-red-zone` is scaffolding carried until that repair
lands rather than the shipped answer.

WP-43 executes it and prices it. This record settles which of the two repairs
`AGENTS.md` reserved is the destination, and settles nothing about what it
costs.

## Why this and not the flag

The flag works and it is not free. It costs a stack adjustment in every leaf
function on the platform, forever, and it announces in every one of those
prologues that this is not quite the ABI the object file claims to conform to.
A platform whose whole purpose is that a Linux userland builds against it
unchanged should not have a permanent asterisk of that shape.

It also does not reach everything. A compiler flag governs what the compiler
emits, and hand-written assembly is outside it permanently; WP-16 delivers a
ledger of exactly that residue, which is the honest measure of what the flag
leaves open. The delivery repair closes compiled and hand-written code at
once, because it fixes the layer that does the damage rather than the layers
that suffer it.

And the damage is ours. Spike 3 measured the host leaving the reserved bytes
alone under preemption, thread hijacking and its own exception dispatch, and
measured Cygwin's delivery taking the word at `%rsp-8` on every single
delivery. Windows is not the obstacle. The obstacle is code this project
already means to modify.

Spike 7 then showed the repair is reachable rather than theoretical. A frame
built 128 bytes below the interrupted stack pointer left the red zone whole
across two thousand deliveries with the nearest write at offset 136, the
handler still ran and returned, a value carried only in the red zone survived
a million folds, the reservation composed under nesting, and an alternate
stack needed none.

## Why now, when the price is not known

Because the direction was already being acted on and was not written anywhere
a reader could find it.

WP-13 shipped `-mno-red-zone` as a target default on 2026-08-29. Anybody
reading that patch, or the rpm macro set, would reasonably conclude the flag
is this platform's answer to the red zone. The statement that it is not lived
in an untracked working note, which `AGENTS.md` forbids a tracked file from
citing precisely because a reader who clones this cannot open it.

A direction that governs how three work packages are written, and that a
reader cannot discover, is not recorded. This record fixes that and nothing
else.

## What is not decided

The cost. Spike 7 measured a model of delivery built on spike 3's hijack, not
Cygwin's real `sigdelayed`, in the same way `spike/gs-thread-pointer/` measured
a stand-in for `_my_tls`. WP-43 re-measures against the real path.

The mechanism. Where in the delivery path the reservation goes, and whether it
is one site or several, is WP-43's to find.

Whether `-mred-zone` survives. The option stays available on the toolchain for
now because WP-43 needs it: pricing the repair means compiling a world that
uses the red zone. What happens to the option afterwards is a question for
whoever writes the record that retires the flag.

## What it costs to reverse

Almost nothing, which is unusual for a record here and worth stating plainly.
This decision changes no artifact today. The flag stands as policy either way,
every package still builds with it, and reversing means continuing to do what
is already being done. That asymmetry is why taking the direction early is
cheap and leaving it unwritten was not.

Reversal is a new record, not an edit to this one.

## When to reopen this

Against WP-43's measured cost, read in these bands. They are a judgment rather
than a measurement, and they are written before the number exists so that the
number cannot be read to suit.

| Added cost per signal delivery | Reading |
|---|---|
| under 5% of the existing delivery path | Proceed. The flag comes off and this record is superseded by WP-43's. |
| 5% to 20% | Proceed, and record the cost in WP-43's own record so a later reader knows what was bought. |
| over 20% | Reopen. Signals are not rare in the workloads this platform exists to run, and a fifth of every delivery is a poor trade for a prologue instruction. |

Reopen also, whatever the number, if the repair turns out to need changes
outside the delivery path. The premise of this record is that one layer does
the damage and one layer can be fixed; if reserving the bytes means touching
every place Cygwin builds a frame on an interrupted stack, that is a different
scope and a different decision.

## Where it is written down

`AGENTS.md`, under the reserved decisions. `doc/IMPLEMENTATION-PLAN.md`, WP-13
and WP-43. `toolchain/gcc/README.md` and the target header the patch installs,
where the flag is defaulted. `toolchain/rpm/macros.elfsysvnt`, where it is
repeated into every build log.
