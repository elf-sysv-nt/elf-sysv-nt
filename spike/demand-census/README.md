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

## The split: why a package is out

`./split-demand.py --root WORK` takes the same per-package demand and answers
a second question the first one hides. "Touches bucket 4" collapses two facts
with different fixes. A package needing `epoll_ctl` needs a capability the
floor beneath does not have, and no amount of veneer work reaches it. A package
needing `_IO_putc` needs glibc's internal ABI, which Cygwin has no reason to
export under glibc's names and which a glibc port answers with glibc's own
code. Only the first is "not provided by Cygwin" in the sense that decides
anything.

The discriminator is the fourth-bucket inventory's own category column applied
per package rather than per symbol: `public-absent` is the real gap and every
other category is plumbing. It reads committed tables plus the demand the
census already collected, runs offline in seconds, and needs no network. It is
also independent of the claimed surface by construction, because the question
is what the floor lacks rather than what the veneer chose to export.

`results-split-2026-09-03.txt` is the recorded run, and it moves the headline
number. Over the 3046 packages that link a glibc soname — not the 4855 scanned,
a third of which carry no 64-bit ELF at all — 1898 need a capability the floor
lacks, 625 need only glibc's internals, and 516 are reachable today. That is
62.3% in the band this document reserves for a program-level review, against
the 52.1% the whole-set share reports. The share over packages that link glibc
is the honest one; the whole-set share is diluted by packages that were never
participants.

The split also prices the `glibc-gs-nt` port precisely: it converts the
internals-only class, 625 packages, and does nothing for the 1898.

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
