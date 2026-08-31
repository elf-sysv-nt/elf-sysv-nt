# Proposal — retire `-mno-red-zone`; the delivery-site repair is the answer

Status: draft
Author: Philip Dye
Date: 2026-08-31
Analysed against: 4216062 on `march`

Drafted at the operator's request. DR-0006 chose to repair the red zone at
Cygwin's signal-delivery site rather than compile around it, and carried
`-mno-red-zone` as scaffolding "until that repair lands rather than the shipped
answer." WP-43 built the repair, certified it on the primary root, and has now
priced it. The price is in the band DR-0006 reserved for pulling the flag. This
proposes pulling it.

## What DR-0006 reserved, and what the number is

DR-0006 settled the direction and reserved one thing: the reading of WP-43's
measured cost against a table it wrote before the number existed, so the number
could not be read to suit. Under 5% of a delivery, the flag comes off and
DR-0006 is superseded; 5 to 20%, proceed but record the cost; over 20%, reopen;
and reopen at any number if the repair needed changes outside the delivery path.

The measurement, taken on the repaired instrument after this session root-caused
a self-kill in the old harness that had made it untrustworthy: median **−0.49%**
over twenty clean runs, mean −2.96%, the tails scheduling noise around zero. It
sits inside the under-5% band. The repair stayed in the delivery site, so the
out-of-path trigger did not fire. Run through `doc/decision-ladder.md` the
reading resolves to a single survivor -- proceed, the flag comes off -- without
reaching tier 8. What remains is the operator's signature on the consequence,
which is why this is a proposal and not a fact read off the number.

## Why the repair makes the flag redundant

`-mno-red-zone` costs a stack adjustment in every leaf function on the platform,
forever, and it does not reach hand-written assembly at all -- the residue
WP-16's ledger exists to bound. The delivery-site repair fixes the layer that
does the damage, so it buys the psABI's 128 bytes back for compiled and
hand-written code at once. With the repair certified and its cost negligible,
the flag is a permanent tax that guarantees less than the thing that replaces
it. It is scaffolding whose building is finished.

## What retiring it means, and costs

Retiring the flag is a recompile of the world without it: every package rebuilt
so its leaves stop reserving the red zone by hand and rely on the delivery
repair instead. That is the cost DR-0006 meant when it said reversing this
"costs a world," and it is why the reading was banded so carefully and why the
signature is reserved. `-mred-zone` was kept available on the toolchain for
WP-43's measurement; once the flag is retired, whether that option stays is a
question for this record's implementer, and WP-16's residue ledger closes with
the flag it was bounding.

## What this settles and what it leaves open

It settles that the flag's replacement is proven and priced, and that by
DR-0006's own criteria the flag comes off. It does not, by itself, perform the
world recompile: accepting it authorizes that rebuild and supersedes DR-0006
with the new record, and the rebuild is scheduled work, not a side effect of the
acceptance.

## Not verified

That every package's leaves in fact stop touching the red zone once the flag is
gone -- the recompile is the measurement, and a package that still assumes the
128 bytes for reasons unrelated to the compiler flag would surface there. WP-16's
ledger is the list to check the rebuild against.
