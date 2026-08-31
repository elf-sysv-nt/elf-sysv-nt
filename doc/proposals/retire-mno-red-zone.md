# Proposal — retire `-mno-red-zone`; honor the red zone as the psABI requires

Status: draft
Author: Philip Dye
Date: 2026-08-31
Analysed against: 4216062 on `march`

Drafted at the operator's request. DR-0006 chose to repair the red zone at
Cygwin's signal-delivery site rather than compile around it, and carried
`-mno-red-zone` as scaffolding "until that repair lands rather than the shipped
answer." WP-43 built the repair and certified it on the primary root. The
scaffolding's building is finished. This proposes taking it down, because
keeping it up is the less faithful implementation of the ABI this project
exists to present.

## The reason: fidelity, not cost

The red zone is the System V AMD64 psABI: the 128 bytes below `%rsp` that a
conforming leaf function uses for its frame without adjusting the stack
pointer, and that a conforming platform must not clobber across a signal.
`-mno-red-zone` makes this platform's own compiler *avoid* the red zone -- every
leaf adjusts `%rsp` instead. That is a deviation from the standard code shape,
and it only ever protected the platform's own binaries. The el8 binaries this
project runs are compiled normally, *with* the red zone; the flag does nothing
for them. So a platform built on the flag is unfaithful twice over: its own code
does not look like System V code, and the guarantee the el8 world actually
depends on is absent.

The delivery-site repair fixes the layer that does the damage, so the red zone
is honored for everyone -- the platform's own code and the vendor binaries
alike. That is the conforming implementation, and it is the whole point of the
effort: a Linux userland builds and runs against this platform unchanged, red
zone included. With the repair in, the flag is not merely redundant; it is an
active infidelity, keeping every leaf on the platform emitting an adjustment
that a real System V leaf does not, for a guarantee the repair already provides.
DR-0006 said this in its own words: the flag "announces in every one of those
prologues that this is not quite the ABI the object file claims to conform to,"
and a platform whose purpose is unchanged Linux builds "should not have a
permanent asterisk of that shape." Retiring it removes the asterisk.

## The cost closes the only objection; it is not the reason

Cost of recompilation is not, and has never been, a deciding factor in this
project. The one thing a measurement could have shown is a *reason to keep* the
flag: if reserving the red zone in delivery were expensive, the cheaper
compile-time avoidance might be worth its infidelity. WP-43's measurement closes
that door. The reservation cost read a median of −0.49% of a delivery over
twenty clean runs, inside DR-0006's under-5% band, with the repair contained in
the delivery site so no out-of-path concern arises. There is no cost standing
between the platform and the faithful implementation. Run through
`doc/decision-ladder.md`, the reading resolves at tier 1 -- correctness,
faithfulness to the psABI -- to a single survivor, retire, without reaching
tier 8.

## What retiring it is

A rebuild of every package without the flag, so the platform's leaves use the
red zone as System V code does and rely on the delivery repair to keep it. That
rebuild is the work of realizing the faithful implementation, not a weight
against it. `-mred-zone` was kept on the toolchain for WP-43's measurement;
after retirement its fate is the implementer's, and WP-16's residue ledger --
which existed to bound the hand-written assembly the flag never reached --
closes with the flag it was tracking, because the delivery repair covers
compiled and hand-written code at once.

## What this settles and what it leaves open

It settles that the faithful implementation is proven and that nothing but the
scaffolding stands in its way. Accepting it supersedes DR-0006 with the new
record and authorizes the rebuild; it does not perform the rebuild, which is
scheduled work rather than a side effect of acceptance. It is reserved to the
operator because it is the ABI boundary, not because the reading is close --
the reading is not close.

## Not verified

That every package's leaves in fact stop touching the red zone once the flag is
gone. The recompile is the measurement, and a package that still assumes the 128
bytes for a reason unrelated to the compiler flag would surface there; WP-16's
ledger is the list to check the rebuild against.
