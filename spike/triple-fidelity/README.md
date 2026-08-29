# The vendor-field count

How many el8 packages mishandle a nonstandard vendor field? One, and the fix
is a line. `results-2026-08-29.txt` is the transcript and the section below it
is the reading. Two scripts here take the measurement --
`fetch-host-tests.sh` puts el8 source in front of the probe one package at a
time, `count-vendor-misses.sh` is the probe and the classifier -- and what is
kept is the means of taking it again.

The triple was settled on 2026-08-29 as `x86_64-elfsysvnt-linux-gnu`, ahead of
this count rather than by it, so what the count does now is price a decision
instead of make one. `doc/decisions/0001-target-triple.md` carries the share
of affected packages at which it should be reopened.

The target triple has four fields and only two carry weight. `config.sub`
passes an unrecognized vendor through untouched, so `x86_64-elfsysvnt-linux-gnu`
puts the project's name where it costs least and leaves `linux-gnu` standing in
the fields configure actually reads. Buildroot has shipped
`x86_64-buildroot-linux-gnu` on that reasoning for over a decade. The residual
cost is the package that tests for a literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` and misses, silently, taking a branch nobody intended.
Such packages exist. How many is the size of the patch set the decision
commits to, and guessing at it is the habit these spikes exist to break.

## Running it

Point it at a tree of unpacked sources, one package per top-level directory.

    ./count-vendor-misses.sh -r /path/to/el8-src -o results-$(date +%F).txt

Two probes over one walk. Each `config.sub` found is fed three triples and its
verdict recorded; the same walk greps for literal vendor matches in the places
a host test lives. Nothing is built, nothing is installed, no privilege is
wanted. Expect the grep to dominate the runtime on a large tree.

`--terse` prints the summary block alone, one `key=value` per line, which is
the form to quote in a document. `--verbose` names every offending package
rather than only counting it, which is what you want on the first run and not
on the twentieth.

## Getting the sources

`fetch-host-tests.sh` supplies the tree, or rather supplies the probe without
one. It reads a source repository's `repodata`, then takes each package in
turn: fetch the `.src.rpm`, verify the checksum the metadata gave, unpack it,
extract the host tests out of whatever archives fall out, run
`count-vendor-misses.sh` over the result, keep the raw probe, delete
everything else. Peak disk is one package.

    ./fetch-host-tests.sh --dest /c/-/el8 --dry-run
    ./fetch-host-tests.sh --dest /c/-/el8
    ./count-vendor-misses.sh --dump /c/-/el8/dump -o results-$(date +%F).txt

The default repositories are Rocky Linux 8.10's four source trees. Rocky
rather than Red Hat's own because `config.sub` arrives inside the upstream
tarball, which a rebuild carries unchanged, and an entitlement buys nothing
for a file neither party touched.

Two numbers decide how the run is shaped, and the dry run prints both. Those
four repositories carry 5816 builds, 174 GB, of 2893 source names: 73 kernels,
35 firefoxes, an accumulation of updates and module streams over the same
upstream tarballs. Taking the newest build of each name is 24 GB, and
`--all-versions` is there for anyone wanting to check that the other 150 GB
would have said the same thing.

The second is scope, and it was taken for a real loss until it was measured.
`--extract host-tests`, the default, pulls only `config.sub`, `config.guess`,
`configure`, `configure.ac`, `configure.in` and the m4 out of each tarball, so
the literal-vendor grep sees less of a package than a walk of the whole source
tree would show it. `--extract all` is the wider scope and is not affordable:
`389-ds-base`
alone unpacks a 49 MB SRPM into 521 MB across a vendored `node_modules` and a
cargo cache, and the grep over that had not returned in ten minutes here.
Every transcript names the scope it ran under as `extract_scope`.

That narrowing was priced on 2026-08-29, forty packages each way, and the
answer came back the opposite way round from the worry. The narrow pass took
2 minutes 3 seconds and reported no literal-vendor hits. The wide pass took 17
minutes, seven and a half of them on `389-ds-base` alone, and reported 108
hits across five packages — 12.5% affected, which is over the share at which
DR-0001 says the triple gets reconsidered.

Every one of those 108 was noise. 102 sat under a Rust `vendor/` tree, where a
`Cargo.toml` naming `x86_64-unknown-linux-gnu` means Rust's own target triple
and nothing `config.sub` will ever read; the rest were a generated `libtool`,
a `README`, a `doc/INSTALL`, a CI yaml, and one top-level `Cargo.toml`. Not
one was a live `case $host`. So the wide scope does not find more signal here,
it finds more noise, and it would have bought a reopened decision with false
positives. The exclusion list in `count-vendor-misses.sh` now covers those
shapes, and `t/lit-noise/` holds one package per shape so that dropping any
single rule shows up as a count of two rather than one.

Replayed against those rules, 107 of the 108 fall and one stands: a CI setup
script naming a Rust target. It stands on purpose. Hand-written shell is the
one place a genuine hand-rolled host test could hide, so excluding `*.sh`
would cost signal to buy quiet, and the literal figure keeps its old warning —
it is an upper bound, and a nonzero one wants reading before it is quoted.

Forty packages off the head of a C-sorted list is not a sample, so read this
as pricing the runtime and characterising the noise, not as evidence about how
many el8 packages hand-roll a host test.

`--jobs` sets how many packages are in flight, four by default. One at a time
the run is bounded by a single connection to a single mirror, which measured
1.2 MB/s here on 2026-08-29 and put 24 GB at five hours. Nothing in a package's
handling reaches another package -- its own scratch, its own fragment, its own
marker -- and the aggregate is assembled from sorted filenames once everything
has finished, so the dump does not depend on the order things complete in.

`t/run-parallel-check.sh` fetches a dozen packages serially and the same dozen
four at a time, then diffs the two dumps. It is separate from
`t/run-tests.sh` because it needs the network. What it catches is jobs
treading on each other: point two of them at one scratch directory and the
parallel side harvests two packages of twelve while the serial side gets all
twelve, which is the check going red as loudly as anything does. What it does
not catch is the sorted assembly. Removing that sort leaves it green, because
at a dozen small packages the completion order and the sorted order coincide.
Read the check as proving isolation, not ordering.

Much past eight jobs you are queueing on the mirror instead of going faster.

On this machine it bought nothing at all, which is worth recording so nobody
reaches for it again expecting otherwise. Measured 2026-08-29 over 40-second
windows: one job gave 1.2 MB/s and 34 packages a minute, six jobs gave
1.06 MB/s and 31, and twelve jobs completed nothing for two minutes because
twelve dotnet source packages of several hundred megabytes each were sharing
the same 10 Mbit link. The constraint here is the link, and concurrency only
divides it into smaller pieces while multiplying what a restart throws away.
On a faster line the option earns its place; here the run is set to one.

A run holds a lock on its destination, skips packages it has already done, and
reaps a dead run's working directory before starting. On a signal it takes its
jobs down before cleaning up, because an orphaned job goes on writing into a
directory the next run will delete, and could lay down a marker for a package
whose fragment never finished — which the next run would then skip. That last
path has no automated check; it was watched once, by killing a run and
confirming every marker still had a fragment beside it.

Resumption is the point of all of it. 2893 packages is long enough that a
network drop is expected rather than feared.

## Reproducing it away from the tree

`--keep-dump FILE` writes the raw probe as the walk runs, and `--dump FILE`
classifies a recorded probe instead of walking anything. A transcript can then
be regenerated from the same bytes months later, on a machine that has no el8
sources at all.

## The fixture

`t/run-tests.sh` classifies `t/sample-dump.txt` and compares the summary
against counts worked out by hand. Five synthetic packages cover the shapes
that matter: a clean accept, a silent rewrite, a rejection of the candidate
that the masquerade survived, a package with no `config.sub` at all but a
literal vendor in its sources, and a `config.sub` that refuses every triple
put to it. That last one exists to separate the two rejection counts, since
charging the vendor field for a file written before x86_64 existed is the
easiest way to read this measurement wrong.

It also checks what `fetch-host-tests.sh` refuses: a missing destination, a
bad `--extract`, a non-numeric `--limit`, an unknown option, and a destination
another run already holds. The last of those is there because two instances
did overlap on 2026-08-29, and the one behind skipped a package the one ahead
had just finished. Its check was watched failing with the lock removed before
it was trusted passing with the lock in place.

## A preliminary run, and a correction it forced

`results-preliminary-2026-08-20.txt` is not the verdict. It is the script run
against the only `config.sub` files on this machine, nine of them, spanning
automake 1.9 through 1.16 and newlib-cygwin, with timestamps from 2009 to
2021. All nine accepted the candidate byte for byte, which is the expected
result and worth little on its own.

All nine also accepted `x86_64-pc-elfsysvnt`, the os-honest control, and that
was not expected. Two separate reasons, and the second is the interesting one.

The 2013 and 2019 vintages do not validate the `os` field at all, so an unknown
os passes as readily as an unknown vendor. Strict validation arrived with the
2020 rewrite. That much only says the door is unlocked on the vintages el8
ships.

The 2021 `config.sub` in the newlib-cygwin tree does validate, rejects `nt` and
`notarealos` outright, and accepts `elfsysvnt` anyway. It matches `elf*`, an
entry in the recognized-os list that exists for bare-metal ELF targets of the
`i386-elf` kind. Acceptance is an accident of a glob rather than a grant, and
it is worse than a rejection would have been: gcc's `config.gcc` reads
`x86_64-*-elf*` as bare metal, so an os field beginning with `elf` would be
silently routed to a target definition that has no operating system under it at
all. A rejection is a build that stops; this is a build that succeeds and is
wrong.

So the case for the vendor field is stronger than the argument it was made on,
not weaker. It just does not rest where it was thought to. `config.sub`
refusing the triple at the door was never the hazard; the hazard is what
accepts it and what that acceptance then means downstream.

## The verdict, 2026-08-29

`results-2026-08-29.txt`, taken over all 2893 source names in Rocky 8.10's
four source repositories. The number is zero.

Across the 1193 `config.sub` files those packages carry, the masquerade and
the candidate get identical verdicts. Not similar: identical, file by file.
Twenty packages refuse the candidate, and all twenty refuse
`x86_64-pc-linux-gnu` too, because their `config.sub` predates x86_64 and
turns away anything with that cpu in it. `autoconf213` is the clearest case
and its vintage is in its name. The marginal cost of the honest vendor field,
at the gate that was supposed to be the hazard, is nothing at all.

The literal-vendor grep found 19 packages, and the transcript has always said
a nonzero figure there wants reading before it is quoted. Read, on the day:

- `flac` is the one real hit. `configure.ac:189` opens `case "$host" in` and
  its first arm is `*-pc-linux-gnu)`, which sets `sys_linux=true` and defines
  `FLAC__SYS_LINUX`. Under our triple that arm does not match, the define
  never happens, and nothing says so. This is precisely the shape the spike
  was built to find, and it is one package in 2893.
- `valgrind` and its three `gcc-toolset-N-valgrind` siblings match on comment
  lines listing what other distributions call their compilers.
- The seven `java-*-openjdk` packages match one comment in `toolchain.m4`,
  `#    Target: x86_64-pc-linux-gnu`.
- `glibc` matches two comments and one diagnostic message. We do not build
  glibc anyway; the veneer is what stands in for it.
- `autoconf-archive` matches a worked example inside a macro's documentation
  block, and `mingw-pkg-config` matches a `dnl` comment.
- `firefox`, `thunderbird`, `mozjs52` and `mozjs60` match Python test fixtures
  under `python/mozbuild/mozbuild/test/configure/`. Those arrived because
  `tar --no-anchored configure` matches a directory named `configure` as
  readily as a file, which widens the extraction a little and is worth knowing
  before someone reads a `.py` path in a dump and wonders.

So the patch set the decision commits to is one package, `flac`, and the fix
is one line. Read against DR-0001, that is 0.0% at the `config.sub` gate and
one package at the literal gate, both inside the band where the triple stands
without further argument.

Worth adding, because it is the more interesting half: `flac` is already
broken this way for anyone whose triple is not `*-pc-linux-gnu`. Debian's
`x86_64-linux-gnu` and the `unknown` vendor that crosstool-NG ships both miss
that arm. We are not stepping onto new ground; we are stepping onto ground
that was already load-bearing for other people.

## Where the finding goes

It has gone. `doc/elf-technical-breakdown.md` no longer carries the claim as
uncounted, and `doc/ROADMAP.md`, `doc/milestones.md` and
`doc/IMPLEMENTATION-PLAN.md` record spike 5 as run. DR-0001 is untouched,
because a record is what was decided and when, not a place to write the
outcome afterwards; the number is here and in the transcript, and it lands
inside the band where that record says the triple stands.

The one thing this verdict does not settle is whether `flac` gets patched or
carried. That belongs to whoever builds it.
