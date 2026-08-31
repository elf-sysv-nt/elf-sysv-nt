# The demand census (spike 12)

How many el8 packages need a symbol the classification cannot yet stand
behind, and — the by-product WP-56 is waiting on — the demand ranking its
slices are ordered by.

The question is sized by bands set in the plan: under 10% of packages
touching bucket 4 is the tail already planned for; 10% to 40% is WP-56
proceeding with a published compatibility statement; over 40% is a
program-level review. The census also has to name a small vendor package,
one whose whole glibc footprint the early slices can cover, to serve as
WP-56's overall done-when.

## What is measured

Every binary package in the Rocky 8.10 x86_64 set (BaseOS, AppStream,
PowerTools, extras; the newest build per name). For each one, every 64-bit
ELF object in its payload is read for its versioned undefined symbols, and
the bindings that resolve to a glibc soname are kept as that package's
demand. The demand is then sorted against the WP-52 classification
(`veneer/classification/classification.tsv`): a package "touches bucket 4"
if any binding it needs is one the classification can only stub — or one
the classification does not know at all, which is counted with bucket 4
rather than quietly better.

Nothing is installed and nothing is run; the census reads containers. The
rpm container, the newc cpio walk, and the ELF version plumbing are done in
`census.py` directly, so the census needs python3 and a network connection
and nothing else.

## Running it

    ./census.py enumerate -o WORK/worklist.tsv
    ./census.py run --worklist WORK/worklist.tsv --root WORK --jobs 4
    ./census.py report --root WORK \
        --classification ../../veneer/classification/classification.tsv \
        -o results-$(date +%F).txt

`run` streams one rpm at a time: fetch, read, keep the few hundred bytes of
per-package demand under `WORK/frag/`, drop the rpm. A `.done` marker per
package makes the run resumable at the cost of one package. Failures land
as `.err` markers with the reason, and the report names how many there
were. `report` also writes `WORK/demand-ranking.tsv` — every distinct
(soname, symbol, version) binding, its package count, and its bucket — which
is the ranking WP-56's slice order comes from.

`probe` takes one rpm (path or URL) and prints its demand, which is the
form to use when checking a single package's footprint by hand.

Results land beside this file as `results-<date>.txt` when the run over the
full set completes. The 2026-08-31 worklist is 4855 names.

## The tests

`t/run-tests.sh` is network-free: the version compare, the cpio walker and
the rpm container against synthetic archives, and the ELF reader against
`t/fixture.elf` — a committed binary built from `t/fixture.c` with the
cross toolchain against the veneer libc, so its undefined symbols carry
exactly the GLIBC-versioned shape the census reads in the field.
