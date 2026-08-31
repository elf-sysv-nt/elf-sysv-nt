# Proposal — install the faced DLL by rename, not by copy

Status: accepted 2026-08-31; decision record and implementation on the WP-27 branch, landing with it
Author: Philip Dye
Date: 2026-08-31
Analysed against: e862a13 on `march`

Drafted at the operator's request. `runtime/face/build.sh` publishes the faced
runtime with a plain copy: `cp new-cygwin1.dll "$out/elfsysv1.dll"`. A copy is
not atomic. It truncates the destination and then writes twenty-five megabytes
into it, and for the width of that write the file at `$out/elfsysv1.dll` is
neither the old DLL nor the new one but a partial image. Anything that reads it
in that window reads garbage. This proposes installing the DLL by writing a
temporary beside it and renaming, so a reader sees one whole DLL or the other
and never the seam between them.

## Why it matters here

A partial DLL does not fail loudly. `LoadLibrary` on a truncated PE returns one
of the same errors a real defect returns -- 126, 998 -- so a test that catches
the copy mid-flight reports "the crossing failed" with no hint that the cause
was a race rather than the code. This session chased exactly those error codes
as defects in the DLL for hours; the copy window was one of the things feeding
them. A build step that can hand a reader a half-written artifact is a source of
false failures that look like real ones, and the cost of removing it is a
two-line change.

The window is narrow but not hypothetical. The autonomous worker runs long
builds in the background and proceeds while they run; a certification that loads
the DLL can overlap a rebuild that is rewriting it. Even in a strictly
sequential run, a copy interrupted partway -- a killed session, a full disk --
leaves a corrupt DLL in place that the next reader trusts, where a rename would
have left the previous good one.

## The change

Write to a temporary in the destination directory and rename it into place:

    tmp=$(mktemp "$out/.elfsysv1.dll.XXXXXX")
    cp new-cygwin1.dll "$tmp"
    mv -f "$tmp" "$out/elfsysv1.dll"

`rename(2)` within one filesystem is atomic: the destination name points at the
old inode until the instant it points at the new one, with no state in between a
reader can observe. The temporary shares the directory so the rename stays
within the filesystem; a temporary in `/tmp` would cross filesystems and degrade
`mv` back into a copy, which is the bug again. On a failed or interrupted copy
the temporary is discarded and the previously installed DLL is untouched.

## The rule under the change

The specific fix is one `cp`, but the principle generalizes: any build product a
concurrent process may read while it is being produced should be published by
rename, never written in place. It is the same discipline `AGENTS.md` already
requires of the installers -- reseed from a pristine template, never edit in
place -- applied to a build output rather than a config file. Where a later
build step writes another artifact a test or a parallel session may read
mid-write, it should follow the same shape.

## What it does and does not settle

It settles that the DLL install is atomic and removes one source of false load
failures. It is worth doing on its own terms, independent of anything else.

It does not settle the larger fragility the test-VM proposal addresses: the
copy race is a real but minor cause of the crossing's intermittency, and the
major cause is two Cygwin runtimes contending in one process, which no amount of
atomic copying fixes. The two proposals are complementary and neither subsumes
the other -- the atomic install removes a write-race that would flake tests even
in the clean guest, and the guest removes a coexistence fragility the atomic
install cannot touch.

## Not verified

That `mktemp` in `$out` and a same-directory `mv` hold on every filesystem the
build runs on, including a Cygwin path that maps to a Windows share. The
proposal assumes a local filesystem where `rename(2)` is atomic; if the build
tree can live on a share where it is not, the guarantee weakens and wants a
note, though the change is still no worse than the copy it replaces.
