# Segment mapping (WP-32)

Once the parser has proven an object structurally sound, the loader has to put
it in memory: reserve the address space its program headers describe, copy each
`PT_LOAD` segment to the address it was linked for, leave `.bss` as the zero it
needs to be, and give every segment its final protection. This package is that
step. It takes the validated view WP-31 produced and the flat file image it was
parsed from, and returns a placed object — a single reserved region, each
segment at its runtime address, and a record of where everything landed — that
the stages past it relocate, resolve and enter.

It maps and no more. It does not relocate, resolve a symbol, or build the stack
the program is entered on; those are WP-34, the packages after it, and WP-40. It
also does not freeze `PT_GNU_RELRO`, because that range is made read-only only
after relocation has finished writing through it. The range is recorded and a
hook is left for the relocation stage to call when it is done.

## The sequence

The order is spike 2's, and it matters at each step.

The whole object span is reserved and committed as one region, writable. One
region per object rather than one per segment is what lets a later `fork` replay
the placement as a unit, and committing it writable up front is what lets the
copy pass run before any protection is set. The span runs from the lowest
segment's address, rounded down to the host's allocation granule, to the highest
segment's end, rounded up; a link base that is not granule-aligned spends the
bytes between it and the granule below, which is a tax on the reservation and
nothing more.

Each segment's file bytes are then copied to its runtime address. The memory
size past the file size — `.bss` — is left untouched, because a freshly
committed page arrives zeroed and the tail is therefore already zero. The
package does not take that on faith: after the copy it reads the first page of
every segment's tail and fails the whole mapping if a single byte is non-zero,
since a dirty page here would silently corrupt a `.bss` that nothing later
would think to check.

Only then are the protections applied, in a second pass, because two segments
can share a page and a page made read-only before its neighbour is copied would
make the copy fault. The pass walks the reservation and sets each region to the
protection of the segment that owns it, or to no access where no segment does,
so an inter-segment gap faults rather than reads.

## What the runtime can see, and why it has to

The placement is made through the C library's `mmap` and `mprotect`, not through
Win32's `VirtualAlloc` and `VirtualProtect`. This is the difference between this
package and the spike it inherits from, and it is deliberate. Cygwin's `fork`
reconstructs a child's address space by replaying the mappings it recorded, and
it records the ones made through its own `mmap`. A mapping made behind it is
invisible to that replay, so a non-PIE image placed with a raw `VirtualAlloc`
would simply not exist in the child. WP-41's stub and WP-42's fork both rest on
that replay, so the done-when for this package is not that the pages are there
but that the runtime wrote them down: every segment shows up in
`/proc/self/maps` with the protection it was given. The test reads them back out
of that file rather than trusting a query.

## The granule, and the object it turns away

Going through `mmap` buys the visibility and brings a constraint with it. The
host reserves address space at a coarse allocation granularity — 64 KB on the
pinned host — and on that host it is also the granularity at which a protection
can be changed: a protection change snaps to the whole granule and one that does
not start on a granule boundary is refused. The package reads both the page size
and the granularity from the host rather than assuming them, so the same code is
correct on a runtime whose granularity differs.

The consequence is a class of object this host cannot honor: one that places two
segments of unlike protection inside a single granule. There is no way to make
one granule both an executable text page and a writable data page, and widening
it to allow both would hand back exactly the writable-executable region the
protection pass exists to prevent. So such an object is refused, with a
diagnostic naming the two segments and the granule, the same way the parser
refuses a malformed one. This does not reach the el8 binaries the project
targets: they link at a 2 MB max-page-size, which puts every segment in its own
granule with megabytes to spare. It reaches objects linked below the granule,
and the project's own toolchain emits granule-separable images when asked
(`-z max-page-size=0x10000` or larger). The reasoning, and what it does not
settle, is `doc/decisions/0008-mmap-granule-protection.md`.

RELRO is frozen at the same granule resolution, so the hook can cover a little
more than the object marked; `PT_GNU_STACK` is only recorded, since the stack
itself belongs to WP-40 and WP-41 and this package reports whether the object
asked for an executable one, nothing more.

## The address it maps at

Spike 2 found that a 2 MB-aligned image's span at `0x400000` was unavailable in
a warmed Cygwin process every time it was asked, because the runtime allocates
before `main` and Windows hands a based-anywhere request the lowest free region.
That is an ordering problem — reserving the low span before the runtime takes it
— and it belongs to WP-41, not here. This package sidesteps it by honoring an
`ET_EXEC` at whatever base it was linked for and placing an `ET_DYN` at a high
base the caller chooses; the specimens the test maps are linked at
`0x10000000`. Nothing here has solved the low-address problem, and nothing here
needs to.

## The interface

`elf_map` takes the image, its size, the `elf_parsed` from `elf_parse`, and a
base hint used only for an `ET_DYN`. On success it fills an `elf_mapping`: the
reservation's base and size, the load bias, the runtime entry, a record of each
placed segment with the page range that actually took its protection, the
translated RELRO range, the recorded `PT_GNU_STACK` disposition, and the host
granularities in force. `elf_map_protect_relro` freezes the RELRO range and is a
no-op that reports success when there is none, so the relocation stage may call
it unconditionally. `elf_unmap` releases the reservation. The header
`elf_map.h` is the contract; `elf_map_err_name` gives a code a stable name for
test output.

## Building and testing

The mapper is two translation units. `elf_map.c` is the placement and is written
against the POSIX memory interface alone; `host_mem.c` reads the page size and
allocation granularity from `GetSystemInfo` and is kept apart so `<windows.h>`
does not have to sit beside the POSIX headers. Both build under the pinned host
toolchain.

`t/` holds the certification. `t/mkspecimens.sh` builds three static ELF
specimens with the cross toolchain — the same geometry at 64 KB, 2 MB, and 4 KB
max-page-size — and `t/map_test.c` maps each and holds it to the bar three ways
a query cannot: it reads `/proc/self/maps` to prove the runtime recorded every
segment, it touches the pages to prove the protections are real rather than
merely reported, and it enters the image through the `t/enter.S` trampoline to
prove it runs and that `.bss` arrived zeroed. The 4 KB specimen is the control
that must be refused for sharing a granule, and a second placement over the
first is the control that must be refused for landing on an occupied span. The
specimen carries no system calls, because there is no Linux kernel underneath;
it reports through a handshake block and returns. `t/run.sh` builds and runs all
of it and reports through the session monitor. The specimens are regenerated
rather than committed.
