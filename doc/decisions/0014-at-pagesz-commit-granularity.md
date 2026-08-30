# DR-0014 — AT_PAGESZ reports the commit granularity, not the reservation one

Status: accepted
Date: 2026-08-30
Deciding: the WP-40 agent, on a defensible call the operator may revisit
Proposal: none; taken when WP-40 built the auxv

## What was decided

The auxv WP-40 builds reports `AT_PAGESZ` as 4096, the size Windows commits
memory at, rather than 65536, the 64 KB granularity it reserves at. Windows has
two page-like numbers and they disagree: `dwPageSize` is 4 KB and
`dwAllocationGranularity` is 64 KB, and a single `AT_PAGESZ` can carry only one
of them. The one reported is the commit size.

The value is not hardcoded in the builder. It arrives through
`proc_image_params.page_size`, so the runtime chooses it at exec time and a
test supplies it as a fixture. This decision is about what the runtime passes,
which is 4096, and what the differential checks against a real Linux auxv,
which also reports 4096.

## Why the commit size

A program reads `AT_PAGESZ` to do arithmetic, and the arithmetic is almost
always about the granularity at which protection and presence change one page
at a time: `mmap` and `mprotect` lengths, the boundary `mprotect` may fall on,
the page a fault is attributed to, the rounding in a memory allocator's own
`sbrk` or `mmap` arithmetic, `getpagesize` and `sysconf(_SC_PAGESIZE)` which
glibc answers straight from this entry. On this host all of that happens at
4 KB. The C library's `mmap` commits and protects at 4 KB even though a
reservation is placed on a 64 KB boundary, so a consumer that rounds a length
up to `AT_PAGESZ` and calls `mprotect` wants 4 KB; told 64 KB it would round to
a span sixteen times too large and protect memory that is not its own, or fail
outright at a boundary the host accepts.

The 64 KB granularity is real, but it is a property of where a reservation may
start, not of how memory behaves once mapped, and it is WP-32's concern rather
than a program's. WP-32 already reserves on the 64 KB boundary and reports both
numbers through `elf_mapping` for the loader that needs them; a program above
the loader does not, and telling it the reservation granularity would describe
a constraint it never touches while hiding the one it touches constantly.

Reporting the smaller of the two is also the safer error if the choice is ever
wrong. A consumer that rounds to 4 KB when the true unit were larger over-calls
`mprotect` harmlessly at worst; a consumer that rounds to 64 KB when the true
unit is 4 KB reasons about fifteen pages it does not own. The commit size is
both the accurate answer and the conservative one.

## What could reopen it

A consumer in the el8 set that reads `AT_PAGESZ` specifically to learn the
reservation granularity — to place its own `MAP_FIXED` regions on a legal
boundary, say — would be told 4 KB and could choose a base the host refuses.
None is known to, and a program that maps at a fixed address generally learns
the granularity from a failed `mmap` rather than from the auxv, but the case is
the one that would move this. If it appears, the answer is not to change
`AT_PAGESZ`, which the arithmetic case needs at 4 KB, but to expose the
granularity through the channel that constraint belongs to, which is the
mapping surface rather than the auxv.
