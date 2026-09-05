# The veneer's address space

History, not design. This was doc/design/Address-Space.md from 2026-08-30
to 2026-09-05: the address-space protocol of the veneer arc, in which a
Cygwin-derived runtime mapped ELF images through its own `mmap` into a low
window a parent reserved for a suspended child. Proposal 0011 replaced that
arc and proposal 0012 § 10 retired this document with the four records it
homed (DR-0008, DR-0028, DR-0064, DR-0077, all retired by DR-0100). The
address space of record is `doc/design/Architecture.md` § The address space.
One argument here survived into the new design and is stated there:
`AT_PAGESZ` reports 4096 (DR-0014). The text below is as it was, kept because
the reasoning is worth reading and because the window arithmetic was certified
work; nothing in it is authority for present behaviour.

---

Who owns which addresses, when they are claimed, at what precision they are
protected, and what a program is told about all of it.
`doc/design/Architecture.md` § Loader and placement states where this sits;
this is the protocol.

Everything here is one problem wearing four faces. An ELF image built for the
small code model has to live low, the host has its own ideas about low memory,
Windows reserves at a coarser unit than it commits, and a Cygwin process has
already made allocations before the ELF world gets a say. Four of the seven
records behind this document are successive corrections to the same protocol,
which is what a subsystem looks like while it is still being got right.

## Three numbers, three jobs

They are routinely confused, so they are separated first.

| Number | Span | Role |
|---|---|---|
| Low 2 GB | `[0, 2^31 − 2^24)` | the requirement |
| Low 4 GB | below `0x100000000` | the contract |
| `ELF_WINDOW_BASE` for `ELF_WINDOW_SIZE` | `[0x400000, 0x40000000)` | what is actively reserved |

The requirement comes from the x86-64 psABI's small code model, which is el8's
compiler default, and from the default `ld` script basing a non-PIE executable
at `0x400000`. This project's cross toolchain reports
`SEGMENT_START("text-segment", 0x400000)`.

The contract is drawn a factor of two above the requirement, as margin:
`R_X86_64_32` is zero-extended and representable to 4 GB, and hand-written
assembly can sit above 2 GB. It costs nothing, because Cygwin 3.6.10 lives
high — `EXECUTABLE_ADDRESS` is `0x100400000`, `CYGWIN_DLL_ADDRESS` is
`0x180040000`, `winsup/cygwin/local_includes/memory_layout.h` names no fixed
address below either, and every `MEM_RESERVE` in `winsup/cygwin` is high.

The rule the contract states: below `0x100000000`, the process address space
belongs to the guest image. Nothing on the host side may occupy an address
under that line — not a module's image base, not a heap, not a scratch buffer,
not a mapping made in passing. Host-side modules link or rebase above it.
Host-side allocations name a base above it or scan upward, and one that cannot
be placed above the line fails rather than falling back below it.

The reservation is narrower than the contract on purpose. A `MEM_RESERVE` of
4 GB is refused from `_dll_crt0` in a sole-runtime process, measured in
`veneer:spike/reent-realproc-low-window/`, so widening the reservation to the contract
line would trade a rule that holds for one that does not. Discipline covers the
span the reservation cannot.

    #define ELF_WINDOW_BASE  UINT64_C(0x00400000)
    #define ELF_WINDOW_SIZE  UINT64_C(0x3FC00000)   /* 1,069,547,520 bytes */
    #define ELF_STUB_STACK_RESERVE 0x100000         /* 1 MB, against gcc's 2 MB */

The window size is a judgment about how much room the ELF world wants low, not
a measurement of how much it needs.

The stack reserve is not decoration. At the toolchain's default 2 MB the
parent's reservation is refused with `err=487`, because the child's initial
stack lands inside the window; at 1 MB it succeeds. Every stub link passes
`-Wl,--stack,0x100000`.

## Why the parent reserves

An allocation the process makes before the ELF world is reserved is an
allocation made out of where the ELF world has to live. So the window is
claimed before the child runs a single instruction, by the parent, through
`VirtualAllocEx` into a process created `CREATE_SUSPENDED`. The child adopts
what it finds.

Two alternatives were measured and refused. A PE `.CRT$XLB` TLS callback does
not link at all: Cygwin supplies no `_tls_used`, so there is no TLS directory
and no callback slot. A replacement image entry point runs too late — the
initial thread's stack is already at `0x400000`, placed by the kernel before
any image instruction, and below it `cygwin1.dll` has chewed the region into
small mappings. Reserving from `main` is later still. The transcript is
`loader/exec/t/when-2026-08-30.txt`, five routes, and only the parent route
reports `when_result=reserved`.

The refusal is the feature. A stub with no window refuses rather than mapping
the image somewhere else.

## The plain-PE shape, end to end

`dispatch.c` creates the child `CREATE_SUSPENDED`, calls
`elf_window_reserve_in(pi.hProcess, &out->window, base, size)`, and only then
`ResumeThread`. A failure terminates the child with `exec_err_window` and a
diagnostic naming the stack reserve, because that is the usual cause. The stub
then calls `elf_window_adopt` before it reads the file, since reading the file
allocates.

Reservation is a fast path with a reconciling fallback. The whole-window
`VirtualAllocEx` is tried first. On refusal, `reserve_in_around` walks the
window with `VirtualQueryEx` into `elf_region regs[64]`, plans with
`elf_window_plan`, and reserves each free sub-span separately. The planner
leaves a region the child already holds as `MEM_RESERVE` and refuses a
`MEM_COMMIT` occupant, which is not a bare reservation and cannot be
reconciled. This is identification, not eviction.

Placement has to undo that. A Windows reservation cannot be partially
released — the unit of `MEM_RELEASE` is the whole allocation — so the window is
released, the image placed while the space is bare, and the two remainders
re-reserved. Because the window is no longer necessarily one reservation,
`elf_window_yield` surveys with `VirtualQuery` first and releases each
constituent, chosen by `elf_window_release_plan`: it names the base of every
reserved region inside the window, skips a free hole, refuses a committed
occupant, and refuses a reservation overrunning either window edge. A
single-reservation window plans to exactly one release, so the reconciled and
unreconciled cases behave identically.

Nothing may allocate between the release and the placement. That is a contract
the caller keeps, not something the code can enforce.

`elf_window_adopt` confirms coverage rather than a single reservation: every
byte standing `MEM_RESERVE`, no free hole, no committed occupant.

Both planners are pure functions and are certified as arithmetic in
`loader/exec/t/unit.c`, with no live process.

    win_ok  win_err_arg  win_err_refused  win_err_release  win_err_place
    elf_region_free = 0, elf_region_reserved, elf_region_committed

A failed re-reservation returns `win_ok` with `held` left 0. The image is
already placed by then, so the caller decides; the stub prints that the window
remainder was not re-reserved and that the brk region is unprotected, and
continues.

## The sole-runtime shape

Where the faced runtime hosts the process itself there is no parent handover at
all. `elf_window_reserve`'s realproc branch claims the window as bookkeeping
with no host reservation, and `elf_window_yield`'s realproc branch hands the
already-free window straight to the placer — no survey, no `MEM_RELEASE`, no
re-reservation — and keeps it held. Measured in
`veneer:spike/reent-realproc-low-window/`: `region base=0x400000 size=0x200000 free`,
`realproc_mmap_fixed_window=ok at 0x400000`, `verdict=cleared`.

## Segment mapping

Mapping goes through the runtime's own `mmap`, one region per object, and the
reason is `fork`. Cygwin's `fork` replays the child's address space from the
mappings it recorded, and it records the ones made through its own `mmap`. A
`VirtualAlloc` mapping is invisible to that replay, so a non-PIE image placed
that way would simply not exist in the child. The arithmetic was established
against `VirtualAlloc`; what changed is the primitive, not the sums.

The host's `mmap` does not expose Windows' reserve-then-commit split: a
`PROT_NONE` reservation cannot be sub-committed by `mprotect`, and a
`MAP_FIXED` mapping cannot be laid over an existing one. So the whole span is
reserved and committed writable in one call, segments are copied in, and
protections are applied afterward.

    res_base = align_down(lo, granule)
    res_size = align_up(hi, granule) - res_base

The accepted cost is that the alignment gaps between segments are committed
zero pages rather than left reserved: a few megabytes per object, pagefile-backed
and protected to no access.

The reserve uses a bare hint, never `MAP_FIXED`. If the returned address is not
the requested one the region is unmapped and the object refused with
`elf_map_err_reserve`, naming both addresses and the owner of the requested
one. `MAP_FIXED` is avoided because Cygwin 3.6.10 lets it land on an
already-reserved span without re-zeroing, and `MAP_FIXED_NOREPLACE` is not in
3.6.10's headers.

Both size constants are read from the host at run time rather than assumed.
`elf_map_host_page_size()` returns `si.dwPageSize` and
`elf_map_host_granule()` returns `si.dwAllocationGranularity`, both from
`GetSystemInfo`, and both are recorded per mapping in `elf_mapping.page_size`
and `elf_mapping.granule`. On the pinned host they are `0x1000` and `0x10000`.

## The granule refusal

Two `PT_LOAD` segments of unlike protection may not share a host allocation
granule. The check runs over all pairs before a single byte is reserved:

    gi0 = align_down(vaddr_i + bias, granule)
    gi1 = align_up(vaddr_i + bias + memsz_i, granule)
    overlap and prot_of(flags_i) != prot_of(flags_j)  ->  elf_map_err_granule

The diagnostic field is `p_vaddr` and the message names both segment indices
and the granule size.

Coalescing to the union of the two protections is the alternative and it is
refused: it would let every object map, at the cost of a granule readable,
writable and executable at once wherever a text and a data segment meet inside
64 KB. That surrenders the W^X and NX the mapping otherwise holds.

The refusal does not reach el8 binaries, which link at 2 MB max-page-size and
put every segment in its own granule with room to spare. It reaches images
linked below the granule, which is why granule-separable linking is a toolchain
default rather than a hope.

    elf_map_ok  elf_map_err_arg  elf_map_err_span  elf_map_err_granule
    elf_map_err_reserve  elf_map_err_commit  elf_map_err_protect  elf_map_err_bss

`elf_map_err_bss` is in neither the host family nor the image family: it means
a freshly committed page was not zero, which is a broken host assumption and
fatal. It is checked over the first page of every segment's tail past `filesz`.

The protection pass walks the reserved span one granule at a time and sets each
to the protection of the one segment that owns it, or `PROT_NONE` where none
does, so an inter-segment gap faults rather than inheriting a neighbour.

## Protection precision

`AT_PAGESZ` reports 4096 and a program is entitled to read it, but protection
changes go through the runtime's `mprotect`, which separates only at the 64 KB
granule: a change that does not start on a granule boundary is refused, and one
that does snaps to the whole granule.

This is one fact with two faces. The loader's own segment mapping is the first;
a program's own `mmap` and `mprotect` calls are the second, and they are
subject to it identically. A program must not depend on 4 KB protection
precision it can name through `AT_PAGESZ` but cannot obtain. The shapes that
notice are a JIT flipping one page to executable, a garbage collector's
write-barrier page, and a guard page below a stack. Most el8 programs never
re-protect below the granule and are unaffected.

What is preserved is W^X and NX. What is surrendered is 4 KB precision for a
program's own calls, and RELRO precision: the relro range is frozen at granule
resolution, so it can cover slightly more than the object marked.

The granule value itself is not decided here. It is read from the host, so a
base whose `mprotect` separates at the page narrows all of this without a code
change.

## What AT_PAGESZ reports, and why

4096 — the size Windows commits at, not the 65536 it reserves at. Windows has
two page-like numbers and one `AT_PAGESZ` can carry one of them.

A program reads `AT_PAGESZ` to do arithmetic about the granularity at which
protection and presence change one page at a time: `mmap` and `mprotect`
lengths, the boundary a `mprotect` may fall on, the page a fault is attributed
to, an allocator's `sbrk` and `mmap` rounding, and `getpagesize` and
`sysconf(_SC_PAGESIZE)`, which glibc answers straight from this entry. All of
that is 4 KB here.

The error asymmetry decides it. A consumer told 4 KB when the true unit is
larger over-calls `mprotect`, harmlessly. A consumer told 64 KB when the true
unit is 4 KB reasons about fifteen pages it does not own. The 64 KB figure is a
property of where a reservation may start, not of how memory behaves once
mapped, and it is reported through the mapping surface instead.

The value is not hardcoded. It travels `elf_mapping.page_size` →
`proc_image_params.page_size` → the `AT_PAGESZ` auxv entry. The differential
against a real Linux auxv records `AT_PAGESZ: 0x1000`.

If an el8 consumer ever reads `AT_PAGESZ` to learn the reservation granularity
for its own `MAP_FIXED` placement, the answer is not to change this entry but
to expose the granularity through the channel that constraint belongs to.

## Required repairs

Three defects are known, decided on 2026-09-03, and named here so that the
implementation that closes them does not have to rediscover them. Each states
what the tree does, what it must do, and what proves it.

`elf_window_release` must release every constituent reservation. It performs a
single `VirtualFree` at the window base, which against a reconciled window
frees one allocation and leaves the rest standing — the same defect the
placement path was repaired for. The release planner already exists as a pure
function beside the reservation planner; `release` must call it the way `yield`
does. Proof is the planner's existing arithmetic unit test extended to the
release path, plus a reconciled-window case that leaves nothing reserved.

The stub must not allocate the guest stack bottom-up. It calls `VirtualAlloc`
with a null base, which is safe only while the window remainders are reserved,
and the placement path returns success with the window unheld when a remainder
re-reservation fails. In that path the next bottom-up allocation lands in the
guest's window. The stack allocation must scan upward from the top of the
window and fail rather than fall below it, which is the rule the image buffer
already follows. Proof is the low-window step asserting on the stack's own
address, not on a collision, since a bottom-up allocation is not placed
deterministically and a collision-only check passes most runs with the rule
broken.

The enforced line must move to the contract line. The rule is that everything
below `0x100000000` belongs to the guest; the upward scan and the low-window
test both use `0x40000000`, the top of the reservation, so a host-side buffer
anywhere in the 1 GB to 4 GB band satisfies every check in the tree and
violates the rule. Both must read `0x100000000`. The address-space walk at
process initialization — `[0, 4 GB)`, refusing to proceed when anything below
the line is held by the host side — must be built; it is what makes the band
that carries no reservation checkable at all. Proof is that walk, and a case
placing a host allocation at `0x50000000` and expecting a refusal.

Settled by: DR-0008, DR-0014, DR-0028, DR-0064, DR-0077.

## Not verified

The reconciling fallback does not clear a real cygwin-linked child, and the two
records that describe it were never amended.
`veneer:spike/reent-stub-realproc-window-reconcile/results-2026-09-01.txt` reports
`realproc_reserve_in=win_err_refused` against a low window that is
`reserved+committed`, not the bare `MEM_RESERVE` DR-0068 models: two committed
regions at `0x5fc000` and `0x5ff000`. `elf_window_plan` refuses any committed
region, so reservation returns `win_err_refused` and adopt refuses likewise.
DR-0071 sets the whole parent-handover arrangement aside for the sole-runtime
shape and attributes the committed occupant to the foreign-parent arrangement
rather than to a Cygwin runtime as such, but DR-0068 and DR-0069 carry no
amendment and no pointer to it. Read them as live for the plain-PE shape only.

`elf_window_release` was never taught what DR-0069 taught `elf_window_yield`.
It still performs a single `VirtualFree(w->base, 0, MEM_RELEASE)`, which
against a reconciled multi-reservation window frees only the allocation based
at the window base and leaves the rest standing — precisely the bug DR-0069
fixed in `yield`. Its header forbids calling it on a `reserve_in` window and
says nothing about an adopted one, which `elf_window_adopt` now accepts.

The contract line and the enforced line are different numbers. Nothing in the
tree enforces `0x100000000`. `realproc-file.c`'s upward scan starts at
`ELF_WINDOW_BASE + ELF_WINDOW_SIZE`, which is `0x40000000`, and
`loader/exec/t/low-window-stub.sh` asserts against the same figure. A host-side
buffer at `0x50000000` satisfies every check in the tree and violates the rule
this document states. DR-0072 is honest that its own consequence is a scan from
the top of the window, so this is a tension inside the record rather than a
code defect, but the enforced line is a quarter of the stated one.

The address-space walk DR-0072 asks for is not built. What exists checks the
image buffer's own address plus one symptom string, and it skips outright when
the built `elfsysv1.dll` or `crt0.o` are absent, so on a fresh tree the
invariant is not checked at all.

A host-side bottom-up allocation survives in the plain-PE stub. The stub's
`VirtualAlloc(NULL, opt.stack, ...)` for the guest stack names no base and does
not scan. It runs after `elf_window_yield` and is normally safe because the
remainders were re-reserved, but `yield` returns `win_ok` with `held == 0` when
a remainder re-reservation fails, and the stub only prints about that and
continues. In that path the window is bare and the next bottom-up allocation is
the stack.

The refusal DR-0064 asks for is not built. The program-facing `mprotect` is a
plain forwarding thunk, and no wrapper names the granule or this rule; a
sub-granule change returns a bare `EINVAL`. The loader's own refusal names the
granule address and errno but not the granule size.

The region walkers carry fixed capacities that neither record mentions.
`reserve_in_around` and `elf_window_yield` both use 64-element arrays; a window
fragmented past that returns `win_err_refused` or `win_err_release`, and the
release path sets `held = 0` before it fails.

`reserve.h`'s opening paragraph is stale in two ways. It tells DR-0028's
single-release story, and DR-0072 claims it states the guest-ownership rule in
that paragraph, which it does not. The per-function comments are current.

`bzip2-nonpie` is red for a different reason than DR-0072 gives. It is
described as failing at fixed-address loading; the 2026-09-02 acceptance run
has it `ready` and `ran`, failing at the suite stage. The non-PIE image is the
harness's own artifact rather than Red Hat's — the vendor build produces
`e_type=DYN` at a zero load base — so the row exercises the harness, not el8.
