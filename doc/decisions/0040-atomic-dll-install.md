# DR-0040 — the faced DLL is installed by rename, not by copy

Accepted 2026-08-31. Source: `doc/proposals/atomic-dll-build.md`.

## Context

`runtime/face/build.sh` published `elfsysv1.dll` with `cp new-cygwin1.dll
"$out/elfsysv1.dll"`. A copy truncates the destination and then writes 25 MB
into it; for that window the installed path is a partial PE, and a reader --
the crossing test, a parallel session -- that loads it in that window gets
`LoadLibrary` errors (126, 998) indistinguishable from a real defect. This
session spent hours mistaking such races for bugs in the DLL.

## Decision

Install by writing a temporary in the destination directory and renaming it
into place. `rename(2)` within one filesystem is atomic: the name points at the
whole old DLL until it points at the whole new one, never at the seam. The
temporary shares the directory so the rename does not cross a filesystem and
degrade back into a copy. The general rule follows: any build product a
concurrent process may read while it is written is published by rename, the
same discipline `AGENTS.md` already requires of the installers.

## Cost and scope

Two lines in `build.sh`. It removes a class of false load failure and is worth
doing on its own terms. It does not touch what is built, only how it is
published, and does not address the coexistence fragility the sole-runtime
crossing decision covers.

## Where it is implemented

`runtime/face/build.sh`, on the WP-27 branch where that file lives; it lands
with WP-27. This record lands ahead of it, on the trunk, so the decision is
visible before the code that carries it.

## When to reopen

If the build tree is ever placed on a filesystem where `rename(2)` is not
atomic -- a Windows share reached through a Cygwin path -- the guarantee
weakens and this wants revisiting, though the rename is no worse than the copy
it replaced.
