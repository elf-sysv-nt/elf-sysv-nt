# DR-XXXX — every image the platform loads is linked granule-separable

Status: accepted
Date: 2026-09-01
Deciding: the build worker, on WP-56, a defensible call the operator may revisit
Proposal: none; taken when the bzip2 acceptance run halted before entry on the
constraint DR-0008 had already named but never recorded as a requirement.

## What was decided

Every ELF the runtime loads must be linked so that no two `PT_LOAD` segments of
unlike protection share a host allocation granule — 64 KB (`0x10000`) on the
pinned host. In linker terms the image's max-page-size must be at least the
granule. This is a requirement on the image, guaranteed at build time, not a
best effort: the project's cross toolchain carries a default of `0x10000` or
larger, so a package built through the acceptance harness is granule-separable
whether or not its own build system knows to ask. A per-link `-z max-page-size`
is the fallback for a build that overrides the default, not the primary means.
The loader's refusal of a sub-granule image (DR-0008) is the backstop that keeps
a violation loud rather than silent.

## Why this is a requirement, not a preference

DR-0008 settled that the loader applies protection at the host's 64 KB granule,
because segments are mapped through the runtime's own `mmap` so that `fork` can
replay them, and that path separates protection only at the granule. Two
differently-protected segments inside one granule cannot both be honored, and
coalescing them to the union would surrender the W^X and NX the second-pass
protection exists to hold, so the loader refuses. That makes granule-separable
linking a property every loadable image must have.

Until now it lived only as an assumption in prose. `doc/IMPLEMENTATION-PLAN.md`
and `doc/ROADMAP.md` both note that el8 binaries carry the linker's 2 MB
max-page-size default, which puts every segment in its own granule with room to
spare — true for stock el8, and the reason the refusal was expected never to
bite. The bzip2 acceptance run showed the assumption does not cover images the
project builds itself: bzip2 halted before entry with `elf_map_err_granule`,
`PT_LOAD[0]` and `PT_LOAD[1]` sharing a `0x10000` granule. bzip2's hand-written
Makefile passes no max-page-size, so the built image inherited a default below
the granule. An assumption that holds for the vendor's binaries but not for the
ones the harness links is exactly the kind of gap a requirement, recorded and
enforced at the toolchain, exists to close.

## Consequences

The cross toolchain's default max-page-size is set to at least the granule, so
acceptance and package builds inherit it without each Makefile cooperating.
Stock el8 binaries already satisfy it at 2 MB and are unaffected. The loader is
unchanged: it keeps refusing a sub-granule image, now as the backstop to a
build-side guarantee rather than a wall a package can hit by accident. The
acceptance harness need carry no special link flags for the common case.

To verify: measure the cross toolchain's current default and bzip2's segment
layout, set the default where it falls short, and confirm the rebuilt bzip2
clears the granule halt and reaches entry. That measurement is the done-when for
the segment-mapping rung in `acceptance/to-green.tsv`.

## What it does not decide

The granule value itself, which the mapper reads from the host at run time
(DR-0008); on a base whose `mmap` separates protection at the 4 KB page the
requirement narrows to that without a change here. The RELRO and inter-segment
gap precision, which DR-0008 freezes at granule resolution. And whether a stock
el8 package could ever violate the requirement: at the 2 MB default it cannot,
and a vendor image that somehow did would be turned away by the same backstop.
