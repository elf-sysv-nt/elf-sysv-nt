# DR-0002 — el8 source comes from Rocky 8.10 and lives outside the repository

Status: accepted
Date: 2026-08-29
Deciding: the operator
Proposal: `doc/proposals/0001-triple-and-el8-sources.md`

## What was decided

el8 source material comes from Rocky Linux 8.10's four source trees, at
`https://dl.rockylinux.org/pub/rocky/8.10/<repo>/source/tree/` for `BaseOS`,
`AppStream`, `PowerTools` and `extras`. It lands under `C:\-\el8`, which is
`/c/-/el8` from a shell in the RHEL root. None of it is vendored into this
repository, and nothing here depends on it being present.

## Why a rebuild rather than Red Hat

`config.sub` arrives inside the upstream tarball, and a rebuild carries that
tarball unchanged. Where a rebuild differs from Red Hat is the spec and the
downstream patches, neither of which the probe reads. An entitlement in the
loop would buy nothing for this measurement.

AlmaLinux 8.10 at `https://vault.almalinux.org/8.10/<repo>/Source/` is the
cross-check. Its per-package git at `https://git.almalinux.org/rpms/<pkg>`,
branch `c8`, is the instrument for a different question: specs and downstream
patches with real history, which is what anchoring a claim about vendor
behaviour wants. It holds no upstream tarballs — those sit in a lookaside
cache — so it is the wrong vehicle for anything that reads inside one.

Neither tree is frozen. Both had moved within a day of the decision, carrying
accumulated 8.10 updates rather than the GA set, so each run records the
`repodata/repomd.xml` of every repository it read into its manifest. That is
the pin, and without it a transcript cannot be reproduced.

## Why outside the repository, and why that path

Tens of gigabytes of regenerable third-party source does not belong in git,
and it does not belong under `~/repo` either: that tree is git working copies
reached through a symlink chain, and putting bulk data inside it adds a
symlink hop and a path-translation hazard to every build.

The short prefix is arithmetic. The deepest installed path carrying a triple
measures 146 characters as Windows sees it, 156 with the ten the longer vendor
adds, against a `MAX_PATH` of 260. Build trees are deeper than installed ones
and a gcc bootstrap stacks stage directories on top, so `/c/-/el8` at eight
characters is worth having over twenty-odd under a home directory. Free space
was not a constraint: 699 GB on C, measured 2026-08-28.

    C:\-\el8\
      srpm\      what was downloaded, when it is kept at all
      src\       extracted trees, regenerable, deleted freely
      build\     rpmbuild BUILD and BUILDROOT
      log\       run transcripts
      manifest\  repomd snapshots, package indexes, the selection
      frag\      one raw probe per package
      done\      one marker per package harvested

## What it costs to reverse

Nothing. Delete the tree; the next run refetches. What the repository keeps is
the transcript and the script, which is the whole point of keeping a spike.

## Two hazards that belong with the path

Defender scans everything landing under `C:\-\el8`, and a harvest extracts a
great many small files, so the directory wants an exclusion before a full run.
And a few upstream tarballs carry filenames differing only in case, which a
case-insensitive volume merges silently; `fsutil file setCaseSensitiveInfo` on
the affected directory is the remedy, applied where it bites.
