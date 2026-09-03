# DR-0072 — the low window belongs to the guest

Status: provisional
Date: 2026-09-02
Deciding: the operator, on the low-window-contract proposal of 2026-09-02,
accepted in full; the contract line and the split between requirement and
margin are the operator's own call within it
Proposal: `doc/proposals/` carries none for this; it was drafted and accepted
as a working note, and the measurement behind it is `spike/vendor-hardened-build/`.
The finding that settled it is in this record: the occupant of `0x400000` was
the loader's own image buffer, not the faced runtime.

## What was decided

Below `0x100000000`, the process address space belongs to the guest image.
Nothing on the host side may occupy an address under that line: not a module's
image base, not a heap, not a scratch buffer, not a mapping made in passing.
Host-side modules link or rebase above it, host-side allocations name a base
above it or scan upward for one, and a host-side allocation that cannot be
placed above the line fails rather than falling back below it.

Three numbers, with three different jobs, and they are not interchangeable.

The requirement is the low 2 GB. The x86-64 psABI's small code model — el8's
compiler default, and the model every package in the 2900 is built under unless
it says otherwise — puts every link-time-known code and data address in
`[0, 2^31 − 2^24)`. The default `ld` script bases a non-PIE image at
`0x400000`; measured on this project's cross toolchain, which reports
`SEGMENT_START("text-segment", 0x400000)`. That is what el8 needs, and it is
the only part of this record that ELF and the vendor between them actually
oblige.

The reservation is the low 4 GB. The extra 2 GB is margin, and naming it as
margin is the point of stating both. It covers `R_X86_64_32`, which is
zero-extended and therefore representable up to 4 GB rather than 2; it covers
hand-written assembly and non-conforming code that sits above 2 GB without any
linker objecting; and it costs this project nothing measurable, which is the
argument that settles it. Cygwin 3.6.10's own layout begins at
`EXECUTABLE_ADDRESS 0x100400000` and `CYGWIN_DLL_ADDRESS 0x180040000`, and
`winsup/cygwin/local_includes/memory_layout.h` names no fixed address below
either; every `MEM_RESERVE` in `winsup/cygwin` is high. So the faced runtime
already lives above a 4 GB line, and drawing the contract there asks nothing of
it.

The actively reserved window is unchanged: `ELF_WINDOW_BASE 0x400000` for
`ELF_WINDOW_SIZE 0x3FC00000`, the one gigabyte `reserve.c` takes and DR-0028's
parent hands over. The contract is wider than the reservation on purpose. A
`MEM_RESERVE` is refused at this size from `_dll_crt0` in a sole-runtime
process — measured, `spike/reent-realproc-low-window` — so widening the
reservation to 4 GB would trade a rule that holds for one that does not.
Discipline covers the span the reservation cannot.

## Why a contract rather than a negotiation

Every PE this project links carries `.reloc` and can be rebased at will. A
vendor `ET_EXEC` cannot move by construction: its `PT_LOAD` addresses are the
only addresses it is honored at. When two images want one address, the one
carrying relocations is the one that yields, and it yields the same way every
time, so there is nothing to decide per image. Writing that down as a contract
rather than resolving it per collision is what converts an unbounded series of
placement conflicts into one rule with one assertion behind it.

The measurement that produced this record is the argument for it. WP-56 spent a
session parked at tier 8 on a placement conflict at `0x400000`, scored against
three candidates that each reversed an accepted decision. The occupant was the
loader's own image buffer: `rp_slurp` read the ELF into
`VirtualAlloc(NULL, ...)`, Windows satisfied that out of the lowest free
region, and the lowest free region was the window the image needed. The region
was anonymous, so it had no module to name and read as something the runtime
had mapped. `loader/exec/reserve.h` states this rule in its own opening
paragraph and `realproc-file.c` names it as an invariant it depends on; the
DR-0071 realproc branch stopped honouring it, and nothing noticed, because
nothing checked.

Wine and Linux both land in the same place from different directions. Wine
reserves the contested ranges in a first-stage preloader before anything else
maps; Linux keeps the `ET_EXEC` window clear as policy, putting interpreter,
stack, and mmap arena high. Neither discovers the conflict at map time, which
is the property this record is after.

## Consequences

`rp_slurp` allocates above the line by scanning free regions upward from the
top of the window; a scan that finds nothing returns failure rather than
dropping below the line. The placement refusal in `elf_map` names the occupant
of a contended span — `rp_map_owner`, `VirtualQuery` plus `GetMappedFileName`
in the realproc seam — so a future violation arrives with its cause attached
rather than as an anonymous occupancy. `loader/exec/t/run.sh` carries a
`low-window` step that asserts the invariant directly, on the image buffer's
own address, not on the collision: a bottom-up allocation is not placed
deterministically by Windows, so a check that watches only for a collision
passes most runs with the rule broken.

`acceptance/packages.tsv` carries `bzip2-nonpie`, a deliberately red row that
builds a genuine non-PIE image through the naked Makefile. bzip2 itself no
longer needs fixed-address loading — the vendor ships a PIE, and the harness now
builds it the vendor's way — but the el8 minority that `%undefine
_hardened_build` still does, and a contract without a failing test is a
sentence rather than a guarantee.

DR-0028 becomes this record's mechanism rather than its neighbour. The
parent-reserved low window is one implementation of the rule stated here,
covering the case where the guest is a suspended child; the sole-runtime
process of DR-0071 covers the other, and both are held to the same line. Where
the two disagree in future, this record governs and DR-0028 is the older, narrower
statement of it.

To verify: an address-space walk at process initialization, `[0, 4 GB)`,
refusing to proceed when anything below the line is held by the host side. It
is not built. What exists is the narrower assertion described above, which
checks the one allocation that has ever broken the rule, and the acceptance row
that stays red until fixed-address loading works end to end. The walk wants a
census of what is legitimately mapped low in a faced-runtime process before it
can fail loudly without failing wrongly, and nobody has taken that census.

## What it does not decide

Whether the reserved window should widen from 1 GB toward the contract line.
That turns on whether a larger `MEM_RESERVE` is honored at `_dll_crt0`, which
is one measurement nobody has taken; the contract holds without it either way.

The `ET_EXEC` share of el8. The proposal names `%undefine _hardened_build` and
prebuilt vendor `ET_EXEC`s as the population that still needs fixed-address
loading, and `spike/vendor-hardened-build/` measures one package rather than a
distribution. What that share is decides how much the `bzip2-nonpie` row is
worth, and it is a census, not a decision.

Whether DR-0071's sole-runtime process survives contact with the rest of the
road to green. Placement is cleared under it, and the next obstacle is the
DR-0066 map-and-enter crossing, which is a certified increment. This record
assumes nothing about what that increment finds.
