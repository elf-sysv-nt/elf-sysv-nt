# DR-0008 — segment mapping goes through the runtime's mmap, at its granule

Status: accepted
Date: 2026-08-30
Deciding: the WP-32 agent, on a defensible call the operator may revisit
Proposal: none; taken when WP-32 met the host's mmap semantics

## What was decided

WP-32 places an object's `PT_LOAD` segments through the C library's `mmap` and
`mprotect` rather than through Win32's `VirtualAlloc` and `VirtualProtect`
directly, so that the runtime's own memory bookkeeping records every mapping.
Three consequences follow from the host's mmap semantics and are adopted as the
package's shape:

The object is one committed region, not a reservation with segments committed
into it. The host's mmap does not expose Windows' reserve-then-commit split: a
`PROT_NONE` reservation cannot be sub-committed by `mprotect`, and a `MAP_FIXED`
mapping cannot be laid over an existing one. So the whole span is reserved and
committed in a single writable `mmap`, the segments are copied in, and the
protections are applied afterward.

Protection is applied at the host's allocation granule, which on the pinned
host is 64 KB, not the 4 KB page. A protection change that does not start on a
granule boundary is refused by the host, and one that does snaps to the whole
granule.

An object whose segments carry unlike protection within a single granule is
refused, with a diagnostic naming the two segments and the granule. The loader
will not widen a granule to the union of two protections, because that would
put writable and executable pages in one region and give up the W^X and NX the
mapping otherwise holds.

## Why through mmap, and why this shape

The reason is fork. Cygwin's `fork` replays the child's address space from the
mappings it recorded, and it records the ones made through its own `mmap`. A
mapping made behind it with `VirtualAlloc` is invisible to that replay, so a
non-PIE image placed that way would vanish in the child. WP-41 and WP-42 rest
on the replay, so the mapping has to be one the runtime wrote down. Spike 2
established the arithmetic against `VirtualAlloc`; this package keeps the
arithmetic and changes the primitive to the one the runtime can see.

The single-region shape and the granule are not preferences; they are what the
host's mmap allows, measured before the code was written. The measurements are
in the package's tests, and the refusal is the honest end of the same fact: on
this host two differently-protected segments in one granule cannot both be
honored, so the object is turned away rather than silently mapped with the
wrong protections.

## Why refuse rather than coalesce

Coalescing a shared granule to the union of its segments' protections would let
every object map, at the cost of a granule that is readable, writable and
executable at once wherever a text and a data segment meet inside 64 KB. The
whole point of applying protection in a second pass is to keep those apart, and
a loader that quietly hands back a writable text page has defeated it. The el8
binaries this project targets link at a 2 MB max-page-size, which puts every
segment in its own granule with room to spare, so the refusal does not reach
them; it reaches objects linked below the granule, and for those the honest
answer is that this host cannot separate their protections. The toolchain this
project builds emits granule-separable images when asked
(`-z max-page-size=0x10000` or larger), so the constraint is a link-time one it
can satisfy rather than a wall.

## What it does not decide

The granule value. The mapper reads the page size and allocation granularity
from the host at run time rather than assuming 64 KB, so a runtime whose mmap
separates protection at the page — should the 3.6.10 base differ from the
pinned 3.0.7 the constraint was measured on — narrows the refusal to segments
sharing a 4 KB page without a code change. The value is discovered, not fixed
here.

The low-address ordering problem. Spike 2 found a 2 MB-aligned image's span
unavailable at `0x400000` in a warmed Cygwin process, and this package maps at a
high base to sidestep it; reserving the low span before the runtime allocates is
WP-41's concern, not this record's.

RELRO precision. The relro range is frozen at granule resolution too, which can
cover slightly more than the object marked. That is the same granule fact and is
noted where the hook is defined; if it bites a real object it is revisited with
the rest of the granule question.

The committed gap. Because the whole span is one committed region, the gaps a
2 MB alignment leaves between segments are committed zero pages rather than left
reserved — a few megabytes per object, pagefile-backed and protected to no
access. It is accepted for now and named here so a later measurement can weigh
mapping each segment as its own region against the one-region-per-object shape.

## What it costs to reverse

Cheap while WP-32 stands alone; dearer once WP-33 and WP-40 build on the
mapping it returns. Reversal is a new record pointing back here. The most likely
reopen is the granule refusal softening to a policy the caller chooses, if an
object worth running turns out to be linked below the granule and cannot be
relinked; the mapper already carries the granule as data, so that change is a
new branch in one function rather than a rewrite.

## Where it is written down

`loader/map/README.md`, which carries the reserve/commit/protect sequence and
the visibility guarantee, and `loader/map/elf_map.c`, whose header comment
points here. `doc/IMPLEMENTATION-PLAN.md`, WP-32, where the delivery note cites
this record.
