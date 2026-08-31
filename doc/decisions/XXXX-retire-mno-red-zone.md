# DR-XXXX — `-mno-red-zone` is retired; the red zone is honored at the delivery site

Status: taken by the operator's standing mandate; supersedes DR-0006
Date: 2026-08-31

## What was decided

`-mno-red-zone` is retired as the target default. The red zone is honored the
way the System V AMD64 psABI requires -- a conforming leaf uses the 128 bytes
below `%rsp`, and the platform's signal delivery leaves them alone -- through
the delivery-site repair DR-0006 chose and WP-43 built.

## Why, and why now

This is not a fresh judgement at the ABI boundary. The operator's standing
mandate is that faithfulness to the ELF/System V platform is paramount, and cost
of recompilation has never been a deciding factor in this effort. DR-0006
already chose the delivery-site repair as the destination and carried
`-mno-red-zone` only as scaffolding "until that repair lands rather than the
shipped answer." The repair has landed: WP-43 built it, and
`runtime/signal/t/run.sh` certifies on the primary root that a signal delivered
into a running thread leaves the 128 bytes intact. So the condition DR-0006 set
for pulling the flag is met, and the mandate makes pulling it necessary rather
than optional.

The flag was an infidelity kept for a reason that no longer holds. It made every
leaf on the platform adjust `%rsp` where a System V leaf uses the red zone, and
it did nothing for the vendor binaries, which are compiled with the red zone and
depend on the platform not clobbering it. The delivery repair honors the red
zone for compiled and hand-written code alike; the flag on top of it only
announced, in DR-0006's words, "that this is not quite the ABI it claims to be."

DR-0006 reserved the reading of WP-43's measured cost against bands, so the flag
could not come off on an expensive repair. That door is closed: the reservation
cost read a median of −0.49% of a delivery, inside the under-5% band, with the
repair contained in the delivery site. Run through `doc/decision-ladder.md` the
reading resolves at tier 1, faithfulness, to a single survivor -- retire --
without reaching tier 8. The cost only confirmed there was no reason to keep the
flag; the reason to drop it is the mandate.

## What changes

`toolchain/gcc/patches/0001-add-the-elfsysvnt-target.patch` no longer sets
`MASK_NO_RED_ZONE` in `TARGET_SUBTARGET_DEFAULT`; the compiler defaults to the
red zone like any x86-64 target, with `-mno-red-zone` still selectable as an
explicit opt-out. `toolchain/rpm/macros.elfsysvnt` no longer mandates the flag.
`toolchain/gcc/t/accept.sh` now asserts the faithful behaviour: a spilling leaf
uses the red zone by default and makes room only under `-mno-red-zone`. WP-16's
ledger, which bounded the hand-written assembly the flag never reached, closes
with the flag it was tracking.

## What it costs to realize

A rebuild of the cross gcc so the new default takes effect, and a recompile of
the delivered packages without the flag, so their leaves use the red zone kept
whole by the delivery repair. That rebuild is the work of the faithful
implementation, not a cost weighed against it, and it is scheduled work rather
than part of this record.

## Reversal

Reversal is a new record, not an edit to this one. The `-mred-zone` /
`-mno-red-zone` options both remain on the toolchain, so a package that needs
the flag can pass it; forcing it platform-wide again would be the record that
reverses this.

## Where it is written down

The reasoning is here rather than in a proposal, because the operator's mandate
makes the retirement a consequence to record, not a question to ask. Superseded:
DR-0006, whose direction this completes. Touched files are as listed above,
together with `toolchain/gcc/README.md` and `toolchain/rpm/README.md`.
