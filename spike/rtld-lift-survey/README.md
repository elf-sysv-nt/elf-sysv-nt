# Could the loader have been lifted?

`AGENTS.md` now asks what Linux and GNU already have before anything gets
written. The loader is the place that question was never asked, and the
standing answer turned out to rest on a mistake: `doc/elf-technical-breakdown.md`
recorded glibc's resolver as GPL, so unusable. It is LGPL-2.1-or-later, which
an LGPLv3-or-later tree can take outright. The real obstacle there is coupling —
`_rtld_global`, glibc's own `link_map`, the assumption of being the process's
first mover — and that obstacle is real. But it is a different claim, and it
does not generalise to every other implementation.

FreeBSD's `rtld-elf` is the other complete one. This survey asks whether it
could have been lifted, and what a lift would carry.

## What it measures

Three things, from sources pinned by checksum at `release/14.2.0` rather than
at `main`, because a transcript pinned to a moving branch rots on somebody
else's schedule.

The licence, read out of the file rather than assumed from the project. Both
`rtld.c` and `rtld.h` carry `SPDX-License-Identifier: BSD-2-Clause`.

Whether the versioning path is really there, which is the part glibc has and
musl only partly has — the reason `doc/elf-technical-breakdown.md` gives for
not simply reusing musl's `dynlink.c`. Six entry points are checked by name,
and the verdef and verneed parses with them.

What a lift would drag behind it. The versioning functions' extents are summed
from the source, and `rtld.c`'s includes are split into BSD kernel headers,
rtld's own, and standard C. The include split is the coupling measure: a
header under `sys/` or `machine/` is an interface that does not exist here.

## The finding

`results-2026-09-02.txt`. BSD-2-Clause on both files, every versioning entry
point present, verdef and verneed both parsed, and the path itself is 320
lines out of `rtld.c`'s 6411, against 9 BSD kernel includes.

So it was liftable, on licence, all along. Three hundred lines is the same
order as what WP-36 wrote from Drepper, which means the choice was closer than
the record made it look — and the record made it look further away because it
had the licence wrong.

The verdict deliberately stops at `liftable-on-licence-adaptation-is-the-question`.
Whether to lift now is a decision, not a measurement, and this spike does not
take it. What it does establish is that the next such question gets asked
before the code is written rather than after.

## What this does not settle

Whether a lift is worth doing *now*. WP-36's matcher is delivered and passes
fifteen checks, so the versioning logic is not the gap; WP-44's wiring is, and
no upstream code helps with a seam that is ours. The likely value of
`rtld-elf` here is as a differential oracle for WP-44 — a second complete
implementation to check binding decisions against, the way WP-35 was checked
against glibc's `ld.so` — rather than as source to adopt.

Whether the coupling count understates the work. Nine kernel includes is a
measure of what `rtld.c` names, not of what the versioning functions need;
those 320 lines might touch two of the nine or all of them. Narrowing the
count to the versioning path's own call graph is the next measurement, and it
is the one a real port decision would want.

## Rerunning

    bash survey-rtld.sh -D /c/-/el8/rtld-lift-survey

The sources are kept between runs and reused while the pin matches. `-h`
prints the options.

## Not verified

That `release/14.2.0` is the right tag to judge from. It was chosen because it
is a release rather than a branch tip; whether the versioning path differs
materially in a later release is unchecked, and the pin makes checking cheap.

That the same answer holds for Android's bionic linker, which is the other
permissively licensed candidate and carries `find_verdef_version_index`. Its
per-file headers need the same check-before-lift this one got, and nobody has
done it.
