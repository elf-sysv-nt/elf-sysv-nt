# The veneer surface at the withdrawal

Measured 2026-09-03, on `march` at `344ef33`, the day proposal 0008 was applied
and DR-0079 recorded it. This is a snapshot rather than a statement of what the
surface is: the claimed set widens as packages are pinned to the acceptance set,
and the first pin will make every number below wrong. Read
`doc/design/Requirements.md` § The claimed surface for what the set is, and
DR-0079 for why it is that shape. Read this for what the change cost, at the one
moment both halves could be counted.

The counts are taken from the committed tables rather than from a run, so they
can be retaken; the commands are at the end.

## The partition, before and after

All 4024 rows of the version map, across the nine el8 libraries.

| disposition | before | after |
|---|---:|---:|
| forward-same | 1614 | 96 |
| forward-alias | 323 | 6 |
| shim | 222 | 12 |
| stub | 1797 | 21 |
| scaffold, the version-node anchors | 68 | 68 |
| unclaimed | — | 3821 |

Nothing was reclassified. The first four rows did not change their reasoning
about what stands behind a name, and the left column reproduces exactly under
`classify.py --claim-everything`. What happened is that 3821 rows stopped being
exported. What remains claimed is 135 rows over 111 distinct symbols, the gap
being symbols carried at two nodes, where a compat binding and a default
binding are separate rows of one name.

## Where the 111 came from

| input | names |
|---|---:|
| A, what the acceptance set imports | 45 |
| B, what the startup files require | 0 |
| C, the closure under the alias rule | 8 |
| node retention | 58 |

Input B is empty of new names rather than empty of demand, and the distinction
matters to anyone reading the derivation later. The loader and the runtime are
built freestanding and ask libc for nothing. `crt1.S` and `Scrt1.S` reference
two external names between them: `main`, which no libc provides, and `exit`,
which the acceptance set imports anyway. So B contributes nothing today and the
derivation still computes it, because the emptiness is a fact about this tree at
this moment and not a property of the design. The certification asserts every B
name is claimed for the same reason.

The striking row is the last one. More than half the claimed surface is there
because a version node would otherwise be left without a member, and verneed
matching requires the provider to define the node a consumer names. Those 58 are
not demand. They are the price of staying loadable, and they are what a reader
expecting the surface to be "what bzip2 needs" will not have predicted.

## Per library

| library | rows in map | claimed |
|---|---:|---:|
| `libc.so.6` | 2358 | 86 |
| `libm.so.6` | 1078 | 11 |
| `libpthread.so.0` | 273 | 18 |
| `libnsl.so.1` | 129 | 2 |
| `libresolv.so.2` | 102 | 4 |
| `librt.so.1` | 47 | 7 |
| `libcrypt.so.1` | 16 | 2 |
| `libdl.so.2` | 14 | 4 |
| `libutil.so.1` | 7 | 1 |

`libm.so.6` is the sharpest fall, 1078 to 11, and it is the clearest case of the
retention rule working alone: the acceptance set imports no maths at all, so
every one of those eleven is there to keep a node non-empty.

## What the built library shows

`libc.so.6` carried 2329 emitted symbols beside the 29 node identity objects the
linker produces from the version script — 2358 dynamic symbols in all. It now
carries 86 beside the same 29, which is the 115 the build reports. All thirty
`Provides` lines survive, because the nodes do, and that is the point of the
retention rule rather than a happy accident.

A strong reference to a withdrawn name now fails at link naming it; the same
reference declared weak links clean, which is DR-0073 unchanged.

## What deliberately did not shrink

`veneer/classification/bucket4-inventory.tsv` still holds 1797 rows. It says
what this platform has nothing behind, and withdrawing a name from the export
surface puts nothing behind it, so the inventory is generated from the partition
before the withdrawal rather than after. Generated after, it would read 21, and
the capability-gap record a later glibc port inherits would have evaporated
without anyone deciding to remove it. That file is the reason proposal 0008 §4
exists and the reason `bin/check-carry-forward` names it.

## The residue

Twenty-one claimed stubs, and they are the uncomfortable part of the result.

Twenty of them are node retentions on nodes whose every member is a stub. A node
with nothing behind any of its members cannot offer a retained member with a
body, so those twenty are exported bodiless — the failure mode the whole change
exists to remove, surviving in the one place the correctness constraint forces
it. DR-0079 records the reading and the proposal carries an addendum naming the
two clauses that disagree.

The twenty-first is `__ctype_b_loc`, which is not in that class: it is a filled
stub, the wiring supplies its body, and the acceptance harness has always
credited it.

## Retaking the measurement

From the repository root, against the committed tables and needing no build:

    awk -F'\t' '{c[$5]++} END{for(k in c) print k, c[k]}' \
        veneer/classification/classification.tsv
    python3 veneer/classification/classify.py --claim-everything \
        -o /dev/null --summary
    awk -F'\t' '{c[$2]++} END{for(k in c) print k, c[k]}' \
        veneer/classification/claimed-surface.tsv

The library counts need a build: `veneer/libc/build-libc`, then
`nm -D --defined-only` over the result with the cross toolchain's `nm`.
`veneer/classification/t/reproduce.sh` certifies that both committed tables
re-derive and agree, and that no version node was left empty.
