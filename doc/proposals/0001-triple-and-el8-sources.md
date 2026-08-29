# Proposal 0001 — the triple, and where el8 source comes from

Status: accepted
Author: Philip Dye
Date: 2026-08-28
Analysed against: 852919e on `main`

The target triple is `x86_64-elfsysvnt-linux-gnu`. That is an operator decision
taken on 2026-08-28 rather than a measurement, and it changes what spike 5 is
for: the count now prices a name already chosen instead of choosing between
two. Everything else in this document exists so that the count can be taken at
all. The el8 material it runs against comes from Rocky Linux 8.10, sits under
`C:\-\el8` outside this repository, and is never vendored into it. A new script
fetches that material one package at a time, probes each package, and throws it
away again, so the survey costs bandwidth rather than disk.

## Context and scope

`AGENTS.md` reserves three decisions for the operator, and the triple is the
first of the three; the roadmap's assumed-path table carries it as assumed,
gated on spike 5. The gate has now been lifted from the front of the decision
and reattached behind it. Spike 5 still runs, still counts the same two
things, and its verdict still matters, but as evidence about the cost of a
settled name rather than as the thing that settles it.

Running it needs source. The spike has never been run against anything but the
nine `config.sub` files that happened to be on this machine, which is a sample
of the autotools vintages rather than a sample of el8, and the preliminary
transcript says so at the top.

In scope: the triple itself, where el8 source is obtained and where it lands on
this machine, and the means of running spike 5 over it. Out of scope: the four
other values of the target definition, which WP-10 owns and which want the
triple settled before they are written down.

## Goals and non-goals

Goals. Record the triple somewhere its five later consumers can cite rather
than recall. Name a source of el8 material specific enough that a reader can
fetch the same bytes in a year, and record what pins it. Leave spike 5 runnable
on this machine without holding the whole source set on disk at once.

Non-goals, each of which could reasonably have been a goal:

- Settling the remaining four target-definition values. `EI_OSABI`, the
  `.note.ABI-tag` payload, the loader SONAME, and the `uname` strings all have
  to agree with the triple and with each other, and WP-10 delivers them
  together. Doing one of the five here and four there is how they come to
  disagree.
- Running the full survey. It is hours of transfer, and starting it is the
  operator's to schedule.
- Building a general el8 source mirror. Something will eventually want one, for
  the package builds the whole program is aimed at; a mirror sized for that job
  answers to different requirements than a probe that discards what it reads.

## The design

### The triple

Four fields, and two of them carry the weight. `config.sub` passes an
unrecognized vendor through untouched while `os` and `abi` are what the
machinery reads, so putting the project's name in the vendor slot costs least
and leaves `linux-gnu` standing where configure looks. Buildroot has shipped
`x86_64-buildroot-linux-gnu` on that reasoning since well before this project
existed, at the same width, which makes the shape ordinary rather than novel.

The os field is closed off, and not for the reason a first guess supplies.
`config.sub` does not refuse `elfsysvnt` there; it accepts it, by matching
`elf*`, the entry that exists for bare-metal targets of the `i386-elf` kind.
`config.gcc` then reads `x86_64-*-elf*` as bare metal and routes the triple to
a target definition with no operating system beneath it. A refusal stops a
build. This succeeds and is wrong, which is worse, and it is the stronger
argument for keeping the honesty in the vendor field. Measured against a 2021
`config.sub` on 2026-08-20; the transcript is in `spike/triple-fidelity/`.

What the decision costs to reverse is the reason it earns a record. The triple
ends up in sysroot paths, in the compiler's own installed layout, and in every
build tree that configures against it, so changing it later means rebuilding
what has been built. That is expensive rather than impossible, and the survey
is what would justify paying it.

### Where el8 source comes from

Rocky Linux 8.10, at `https://dl.rockylinux.org/pub/rocky/8.10/<repo>/source/tree/`
for `BaseOS`, `AppStream`, `PowerTools`, and `extras`. Checked on 2026-08-28;
`BaseOS/source/tree/Packages/` had been refreshed that morning. Rocky 8.10 is
the terminal Rocky 8 release, so it stays in `pub` rather than moving to
`vault`, where the tree stops at 8.9.

Red Hat's own SRPMs need an entitlement, and for this measurement they buy
nothing. `config.sub` arrives inside the upstream tarball, which a rebuild
carries unchanged; the places a rebuild differs from Red Hat are the spec and
the downstream patches, and neither is what the probe reads.

AlmaLinux 8.10 is the cross-check rather than the source, at
`https://vault.almalinux.org/8.10/<repo>/Source/`. Its per-package git at
`https://git.almalinux.org/rpms/<pkg>`, branch `c8`, is the right instrument
for a different question: it holds specs and downstream patches with real
history, which is what you want when anchoring a claim about what a vendor did.
It does not hold upstream tarballs. Those live in a lookaside cache keyed off
the `.metadata` file in each repository, so git is the wrong vehicle for this
particular count and a good one for the archaeology around it.

Neither tree is frozen. Rocky's source tree moved on the morning of the run and
Alma's had moved the day before, both carrying accumulated 8.10 updates rather
than the GA set. So the pin is recorded rather than assumed: the harvester
writes each repository's `repodata/repomd.xml` revision and checksums into the
run's manifest, which is what makes a transcript reproducible eighteen months
on.

### Where it lands

`C:\-\el8`, which is `/c/-/el8` from a shell in the RHEL root.

    C:\-\el8\
      srpm\      what was downloaded, when it is kept at all
      src\       extracted trees, regenerable, deleted freely
      build\     rpmbuild BUILD and BUILDROOT
      log\       run transcripts
      manifest\  repomd snapshots and checksum lists

Not under `~/repo`. That tree holds git working copies reached through the
`.primary` symlink chain, and this is bulk regenerable data measured in tens of
gigabytes; putting one inside the other adds a symlink hop and a path
translation hazard to every build for no gain.

The short prefix is the other reason, and it is arithmetic rather than taste.
The deepest installed path carrying a triple measures 146 characters as Windows
sees it, 156 with the ten the longer vendor adds, against a `MAX_PATH` of 260.
A build tree is deeper than an installed one and a gcc bootstrap stacks stage
directories on top of that, so the eight characters of `/c/-/el8` against the
twenty-odd of a path under `/home/phili/repo` are worth having. Free space is
not a constraint: 699 GB on C, measured 2026-08-28.

### The harvester

`spike/triple-fidelity/fetch-host-tests.sh`. It enumerates a source repository
from its `repodata`, and then, for each package in turn: fetches the
`.src.rpm`, verifies the checksum the metadata gave, unpacks it with
`rpm2cpio`, unpacks whatever source archives fall out of that, runs the
existing probe over the result, appends the raw probe lines to one aggregate
dump, and deletes the working directory before moving on. Peak disk is one
unpacked package rather than a source set.

The probe is not reimplemented. `count-vendor-misses.sh` already walks a tree
of one-package-per-directory and already writes its raw probe out under
`--keep-dump`, so the harvester stages each package into that shape and calls
it. The two objects read one implementation of the probe, which is the same
rule the roadmap states for the export list and for the same reason. When the
harvest finishes, `count-vendor-misses.sh --dump` classifies the aggregate and
produces the transcript, exactly as it would have from a walk.

Streaming buys disk and not bandwidth. Every byte of the source set still
crosses the wire, because an SRPM cannot be opened halfway; what changes is
that none of it is kept. A run that stops after four hundred packages leaves
four hundred packages' worth of usable dump behind, which a fetch-everything
-then-walk design does not.

Resumption follows from that. Each harvested package leaves a marker and its
dump fragment under the destination, so a second run skips what the first
finished, `--force` re-does a package regardless, and a working directory left
by a killed run is reaped at start rather than tripping the next one. This is
the idempotency rule `AGENTS.md` states for installers, applied to a fetcher,
where it earns its place: the failure being designed against is a network drop
at package nine hundred.

`--dry-run` fetches metadata and nothing else, then reports the package count
and the total bytes a real run would transfer. That is also how the size of the
el8 source set gets measured, rather than by an estimate that would want
checking anyway. `--limit N` stops after N packages, which is what makes a
sample run cheap enough to be a test.

## Alternatives considered

A reduced tree vendored into this repository — `config.sub` and `configure`
from every package, committed. It fails on the probe's second half: the literal
vendor grep would silently narrow to whichever files were kept, and a
measurement whose scope shrank without saying so in its own transcript is worse
than no measurement. The repository would also be carrying several hundred
megabytes of somebody else's source.

A full unpacked mirror, walked once. Nothing is wrong with it except cost, and
it stays the fallback if streaming proves unreliable in practice. The reason it
is not the plan is that it makes the spike unrunnable until a multi-hour fetch
has completed, and it turns a network failure at hour three into a restart
rather than a resumption.

Red Hat's SRPMs from the CDN under a Developer subscription. An entitlement in
the loop, for bytes that are identical in the part being read.

`git.almalinux.org` in place of SRPMs, which is attractive until you look for a
tarball and find a lookaside pointer. It answers the archaeology question well
and this one not at all.

Masquerading as `x86_64-pc-linux-gnu`, with the honest name moved to
`EI_OSABI`, the `.note.ABI-tag`, the loader SONAME, and `uname`. This is the
road not taken today, and it is recorded here rather than dismissed because the
survey may yet argue for it. The condition under which it should be reopened is
written into DR-0001 as a number instead of an adjective.

## Cross-cutting concerns

Nothing is on disk yet, so there is no migration. Rollback is deleting
`C:\-\el8`, and nothing in the repository depends on its contents; what the
repository keeps is the transcript and the manifest, which is the point of
keeping a spike at all.

The probe executes code from the packages it downloads. `config.sub` is run,
because running it is the only way to learn what it does with a triple, and a
`config.sub` is a shell script that arrived inside an SRPM. Exposure therefore
equals trusting the Rocky mirror, which is the same trust every build of this
program will extend to it anyway; the checksum from `primary.xml` is verified
before anything is unpacked, and the probe runs in a temporary directory owned
by the invoking user. It is worth naming rather than assuming, because the
usual reading of "nothing is built and nothing is installed" is that nothing
foreign runs, and something foreign does.

Two host-specific hazards, both cheap to handle and expensive to discover late.
Defender scans every file that lands under `C:\-\el8`, and with a few hundred
thousand extracted files per run that dominates the wall time, so the directory
wants an exclusion before the first real harvest. And a handful of upstream
tarballs carry filenames differing only in case, which a case-insensitive
volume merges without a word; `fsutil file setCaseSensitiveInfo` on the
affected directory is the remedy, applied where it bites rather than
everywhere.

## Verification criteria

1. `bash -n` under the RHEL root's bash 4.4.12 exits 0 for
   `fetch-host-tests.sh` and for `count-vendor-misses.sh`.
2. `fetch-host-tests.sh --help` exits 0 and prints its `Usage:` block.
3. `t/run-tests.sh` passes unchanged.
4. `fetch-host-tests.sh --dry-run` over the four Rocky 8.10 source
   repositories prints a package count and a byte total for each, and leaves no
   `.src.rpm` anywhere under the destination.
5. A run with `--limit 5` against `BaseOS` produces an aggregate dump that
   `count-vendor-misses.sh --dump` classifies, and the classification reports
   five packages seen.
6. Repeating that run leaves the destination byte-identical and reports five
   packages skipped.
7. `git ls-files -s` reports mode 100644 for every file added except the
   scripts, which are 100755.
8. Every file in `doc/decisions/` has exactly one row in
   `doc/decisions/index.md`, and every row has a file.

## Open questions

Whether the full survey runs against Rocky alone, or against Rocky and Alma
both so that packages where the two rebuilds diverge can be spotted. The answer
costs a second full transfer and buys a cross-check on a part of the tree the
probe barely reads, so it is cheap to defer and dear to settle now.

## Decision log

Recorded as they were taken during implementation, per the acceptance under
which this ran.

- The reopen threshold in DR-0001 is stated as a share of packages carrying a
  `config.sub` rather than as a count, so that it survives a change in the size
  of the source set.
- Booleans in `fetch-host-tests.sh` carry `--no-` counterparts, which
  `count-vendor-misses.sh` does not. The tool reads environment variables that
  can turn a boolean on, and without the negated spelling there is no way to
  turn it off again from the command line. The older script is left alone
  rather than churned.
- The manifest is written by the harvester rather than the classifier. The
  classifier can run from a dump on a machine with no network and no
  repository, which is exactly the case where it could not produce a manifest
  honestly.
