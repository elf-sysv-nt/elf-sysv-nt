# Implementation plan

`ROADMAP.md` says what has to exist. This says in what units it gets built, what
each unit needs before it can start, and how anyone can tell it is finished.
Same assumed path, same three reserved decisions — one of which, the target
triple, was taken on 2026-08-29 and is recorded in DR-0001. The caveat that
stood here — a negative on spike 3 invalidating everything from phase 2 onward
— is discharged: it ran on 2026-08-29 and came back yes.

A work package is the smallest thing worth an entry criterion. Each carries
four lines. Needs names the packages that must be finished first, and an empty
one means the package can start today. Delivers names the artifact, in the tree,
by path where the path is already decided. Done when is a test that either
passes or does not, never a judgment. Risk appears only where the package has
one worth naming in advance.

Packages are numbered by phase and are not renumbered when one is added, so the
sequence has gaps and the gaps mean nothing.

Nothing here is scheduled. Every spike has now answered and none of the answers
reshaped the graph, but a date attached to WP-53 today would still be fiction,
and the dependency order remains the only ordering claim the plan makes.

---

## Phase 0 — the spikes

`milestones.md` is the governing document for these five and it is not restated
here. What belongs in a plan is the rule around them: run one through to its
stated verdict without asking, then stop at the boundary and report, rather than
beginning the work the answer implies.

All five are done, all on 2026-08-29. Spike 3 did not want spike 2's stub in
the end: the crossing is measurable inside one process with two hand-written
callers, and carrying an ELF image into it would have added a variable without
adding a question.

Spike 3 came back yes and left two things behind rather than a schedule change.
Variadic exports cannot forward: System V's `va_list` is a twenty-four-byte
descriptor and Microsoft's is an eight-byte pointer, so WP-24's printf family
unpacks and repasses by value rather than handing a list down. And the red zone
is destroyed by Cygwin's own signal delivery, not by Windows, which leaves
WP-13's and WP-16's `-mno-red-zone` mandates exactly where they were and adds
one option nobody has priced: a 128-byte gap in the delivery path WP-43 owns.
That option is `AGENTS.md`'s to settle.

Spike 4 came back yes and left two conditions on WP-53 rather than a schedule
change: the base `.gnu.version_d` node has to carry the same string as
`DT_SONAME`, since the versioned `Provides` is formatted against the base node
and the unversioned one against `DT_SONAME`, and the whole 29-node ladder has
to be defined rather than a node, since a package requiring `GLIBC_2.14` is not
satisfied by a library that stops at `GLIBC_2.2.5`.

Spike 2 came back yes and left one constraint behind, which WP-32 and WP-41
carry below: a non-PIE image's span has to be reserved before anything else in
the process allocates without a base, because Windows hands out the lowest free
region and a Cygwin runtime allocates before `main`.

Spike 8 came back with a qualified yes and left a subsystem this plan has not
yet cut into a package. An access through a zeroed `%fs` base faults, a
vectored handler registered ahead of Cygwin's emulates it through carrier C3
and resumes, and the interrupted code gets its other registers and its flags
back — so a load-time TLS rewriter for vendor binaries may be a heuristic over
a sound fallback. The qualifier is that the fallback covers the data-movement
forms and refuses the read-modify-write ones, where a missed site is a
`SIGSEGV` rather than a slow success. `doc/proposals/0003-vendor-binary-tls-rewriting.md`
carries the reading and names the census that prices the gap. WP-33's and
WP-54's exit criteria both run a vendor binary and both inherit whatever this
becomes; cutting it into a package is not done here, because that is a decision
about scope rather than a finding.

Spike 1 came back no, which is the branch `milestones.md` reserved and not a
delay. `%fs`-relative TLS is unavailable on this host: the base is writable and
addresses correctly, and Windows returns it as zero after anything that
deschedules the thread, preemption included. What replaces it was settled by
the operator in DR-0003: a runtime-owned thread pointer through `%gs`, carrier
C3, measured by `spike/gs-thread-pointer/`. WP-30's interface was unaffected
throughout, and its body may now be written against that model.

---

## Phase 1 — target definition and toolchain

### WP-10 — the target definition record

Needs: nothing. It waited on spike 5 until the triple was decided without it.
Delivers: `doc/target-definition.md`, carrying five values that must agree — the
triple, the `EI_OSABI` byte, the `.note.ABI-tag` payload, the dynamic linker
SONAME, and the `uname` strings. The triple is fixed by DR-0001 and this
package cites it rather than restating the argument; the other four are open.
DR-0005 later added a sixth thing the record has to carry, which is not a value
but a limit: what the `linux` and `gnu` fields claim, and the single axis where
`linux` claims more than this project delivers.
Done when: every later package that hardcodes one of the five cites this
document rather than a memory of it.

Delivered 2026-08-29. The four open values were settled against measurement
rather than recollection: `spike/vendor-image-shape/` read forty-one el8
binaries first. `EI_OSABI` turned out not to be a value anyone picks, which
is the finding — the linker writes `ELFOSABI_GNU` for an object using GNU
extensions and `ELFOSABI_NONE` otherwise, and el8 splits five to thirty-six
along exactly that line. The exit criterion is mechanical now:
`bin/check-target-definition` greps the tree and exits non-zero on the first
site that carries one of the values without citing the record.

Each of the five ends up compiled into shipped artifacts, so changing one later
means rebuilding the world. Writing them down once, before anything consumes
them, is cheap insurance against discovering in month nine that the loader and
the specs file disagree about a SONAME.

### WP-11 — config.sub and config.guess

Needs: WP-10.
Delivers: the patch against upstream `config`, plus a refresh procedure for the
vendored copies that every source package carries at its own vintage.
Done when: `config.sub x86_64-elfsysvnt-linux-gnu` echoes the input unchanged,
and the refresh script rewrites a vendored copy in a package tree idempotently.

Spike 5 sized this on 2026-08-29 and the news is good: 891 of 2893 packages
carry a `config.sub` at all, 1193 copies between them, and every one of those
copies treats the honest triple exactly as it treats `x86_64-pc-linux-gnu`.
The twenty that refuse the honest triple refuse the masquerade too, on the cpu
field, and predate x86_64. So the refresh policy is about vintage rather than
about the vendor, and this package inherits one patch from the spike: `flac`,
whose `configure.ac` gates `FLAC__SYS_LINUX` on a `*-pc-linux-gnu)` arm.

Delivered 2026-08-29, in `toolchain/`. `config.sub` needed no patch at all,
which was DR-0001's prediction and is now a check: upstream at `2026-05-17`
returns the triple unchanged. `config.guess` needed one, for the reason the
plan did not anticipate — it has to *produce* the triple, and `uname` cannot
tell it, since our `sysname` is `Linux` on purpose. It asks the compiler for
`__ELFSYSVNT__` the way it already asks which libc it is looking at, and falls
back to a marker file where no compiler exists. Two obligations fall out:
WP-13's specs define the macro, and WP-63 installs the marker.

### WP-12 — binutils

Needs: WP-11.
Delivers: `bfd` target vector, `ld` emulation and default linker script, ELF
backend, assembler target.
Done when: `as` accepts `.symver`, `ld` accepts `--version-script` and the
resulting shared object carries `.gnu.version_d` entries that `readelf -V`
prints, a linked object's `EI_OSABI` and `.note.ABI-tag` match WP-10, and no
link produces a `%fs`-relative thread pointer fetch.

That second clause is the whole reason the format changed. It is worth an
explicit test rather than an assumption, because the failure being avoided here
is precisely a linker that accepts the option and silently discards the version
names.

The fourth clause was added on 2026-08-29, after the first three had been met.
There is no port: every pattern binutils matches on is `x86_64-*-linux-*`, so
2.42 configures, builds and passes the three original criteria untouched, and
`toolchain/binutils/t/accept.sh` mechanizes them. What the criteria missed is
that `ld` rewrites the psABI's TLS sequences in place and emits
`mov %fs:0x0,%rax` out of `bfd`, on a host where spike 1 established that base
does not survive a context switch. DR-0003 named WP-30's codegen, WP-37's
loader and WP-13's specs as where the carrier appears, and no linker;
`spike/ld-tls-relaxation/` is the correction.

The repair is to refuse `R_X86_64_TLSGD`, `TLSLD`, `GOTTPOFF`,
`GOTPC32_TLSDESC` and `TLSDESC_CALL` rather than to rewrite them differently,
since the `%gs` chain needs three instructions where the psABI reserves sixteen
bytes for two, and a link error is the honest answer for an input this
toolchain cannot translate. `TPOFF32`, `TPOFF64`, `DTPMOD64` and `DTPOFF64`
stay accepted: they are values rather than sequences, and WP-13 will want
`TPOFF` for sequences of its own.

Vendor objects that already carry the refused relocations are not this
package's problem to solve, only to make visible. They belong to proposal
0003 and to spike 8.

Closed 2026-08-29. `toolchain/binutils/patches/0001` is the refusal and
`t/accept.sh` carries fourteen claims across the four criteria. Where the
check sits was the whole of the difficulty: placed after
`elf_x86_64_tls_transition` it passes general dynamic and initial exec, both
of which arrive there already rewritten, while local dynamic arrives as an
accepted form and links with `mov %fs:0x0,%rax` in the output. It runs ahead
of the transition instead. The test assembles one model per object for the
same reason, since a combined object stops at the first refusal and reports
success.

### WP-13 — gcc, stage one

Needs: WP-12.
Delivers: a cross compiler with no libc, targeting the triple.
Done when: it compiles a freestanding object, and `-mno-red-zone` is on by
default in the specs rather than passed by the caller.

Closed 2026-08-29. Like binutils, almost no port: the triple's os and abi
fields route it through the ordinary x86_64 Linux arm, and `patches/0001`
adds only the two things the target mandates. `-mno-red-zone` by
`TARGET_SUBTARGET_DEFAULT`, and `__ELFSYSVNT__` for the `config.guess` WP-11
taught to ask. `t/accept.sh` carries eleven claims.

The flag is scaffolding rather than the answer, which DR-0006 records and
which the target header now says before it says anything else. The mandate is
a target default rather than a spec string, which is stronger: a spec can be
overridden by a later flag on the same line. `-mred-zone` still
works, deliberately, since WP-43 may retire the flag and a target that refused
the option would have to be rebuilt to find out.

One mistake is worth carrying forward. `i386/unix.h` already keeps `MASK_80387`,
`MASK_IEEE_FP` and `MASK_FLOAT_RETURNS` in `TARGET_SUBTARGET_DEFAULT`, and the
first version of the patch assigned rather than ORed, turning the x87 off.
Configure succeeded, the compiler built, freestanding objects compiled, and it
reported `-mno-red-zone [enabled]` exactly as wanted; libgcc failed twenty
minutes later on `__mulxc3` with an error that reads like a libgcc bug.

Risk: the specs file is where target mandates live, and anything a package can
forget to pass is a mandate rather than an option. One object compiled with a
red zone corrupts a stack under Cygwin's signal delivery -- measured by spike 3
taking `%rsp-8` on every delivery, where the host itself takes nothing -- at an
unpredictable later date, which is close to the worst debugging shape a defect
can take. The flag is not a no-op on this toolchain either: gcc gives a
`sysv_abi` leaf a red zone here unless told otherwise.

### WP-14 — sysroot and startup files

Needs: WP-13, WP-50.
Delivers: the sysroot layout, `crt1.o`, `Scrt1.o`, `crti.o`, `crtn.o`, `libgcc`.
Done when: a static hello links and the spike 2 stub runs it.

Closed 2026-08-29, nine claims of nine. The startup files assemble, the
sysroot lays out with el8's usrmerge links, a static hello links and carries
the right headers, and the spike 2 stub maps it, enters it and gets control
back.

Getting there meant repairing the harness rather than the package. Spike 2
could not link anything -- its README says so -- so everything its stub knew
about image shape it learned from a synthesized specimen with exactly three
`PT_LOAD` segments, one of each protection, and a handshake block at the head
of the writable one. `ld` gives this hello four, and put the image's own
`data_word` exactly where the handshake went. The first thing WP-12 made
possible found a limit in the harness written before it existed, which is
roughly what should happen.

The stub now takes the image's shape from the image and allocates its handshake
page rather than borrowing the image's. Spike 2's summary block still diffs
clean against its committed transcript, so the verdict it recorded is
untouched. `spike/map-and-jump/issue/0001` carries what else surfaced,
including two of this package's own tests that could not have passed: the
`.bss` claim compared `MemSiz` against the flags column, and the stub was
invoked with `--quiet`, which suppressed the output the next claim then
grepped for.

### WP-15 — gcc, full bootstrap

Needs: WP-14, WP-51.
Delivers: the compiler rebuilt against our libc, plus `libstdc++` and whatever
other language runtime the el8 set needs.
Done when: the compiler builds itself, and a C++ program that throws across a
shared library boundary catches on the other side.

That exception test is not decoration. It exercises `dl_iterate_phdr`,
`PT_GNU_EH_FRAME`, and the rule that DWARF unwinding never crosses into the
host-facing core, all three at once.

### WP-16 — build macros

Needs: WP-15.
Delivers: the rpm macro set carrying `-mno-red-zone`, the triple, and the
sysroot paths.
Done when: a package that names no flags gets all of them, and a ledger exists
listing every package in the set with hand-written assembly, since that is where
the red-zone assumption stays live after the macro closes it everywhere else.

Delivered 2026-08-29 in `toolchain/rpm/`, out of dependency order because it
needs nothing WP-15 produces. The macro set is the easy half. `bin/asm-ledger`
is the other, and it has not been run over the el8 set: the source dump spike 5
used is gone from this machine and refetching 2893 packages is hours. The tool
is verified against a constructed tree and against `flac`, which is to say it
works and has not yet been pointed at the thing it exists for.

Two of those Needs lines point forward into phase 5, and that is the ordinary
libc bootstrap cycle rather than a mistake in the graph. WP-50 is a header set,
which can be written before anything implements it, and WP-51 is the first
veneer that links. The loop is broken the usual way: headers, then a compiler
that trusts them, then a library that satisfies them, then the compiler again.

---

## Phase 2 — the runtime

This phase was gated on spike 3, which ran on 2026-08-29 and came back yes, so
the veneer-thunk fallback and its different package list are a road not taken.
What the spike measured is one function's width, not a DLL's, so the packages
below still meet unwind data, `DllMain` and PE TLS callbacks for the first
time.

### WP-20 — the export inventory

Needs: nothing.
Delivers: a generated list of `cygwin1.dll`'s exports with their `SIGFE` and
`NOSIGFE` annotations, their `DATA` markers, and their aliases, extracted from
`winsup/cygwin/cygwin.din` at a named ref.
Done when: the extraction reruns and reproduces the file byte for byte, and both
WP-21 and WP-51 read this one list rather than each keeping its own.

Two objects reading two copies of an export list drift, and the drift shows up
as a symbol that resolves at build time and is absent at run time.

Delivered 2026-08-30, in `runtime/exports/`. The named ref is DR-0007's: Cygwin
3.6.10, `newlib-cygwin` `b11613e47`. `extract-exports.sh` parses `cygwin.din`
into 1767 rows — name, data-or-function, signal-frame class, alias target — and
`t/reproduce.sh` pins the ref and diffs a fresh extraction against the committed
`cygwin-exports.tsv`, which is the byte-for-byte guarantee the criterion asks
for. Two `.din` forms the first cut missed and the strict parser caught:
`glob_pattern_p` carries no annotation and is recorded as `none` rather than
guessed, and an alias `=` may be attached to the name; both are handled and
noted in the README so WP-21 is not surprised by them.

### WP-21 — the down-call wrappers

Needs: WP-20.
Delivers: `ms_abi` wrappers around every imported Windows function, generated
from the import list rather than written by hand.
Done when: no direct call from the System V side to `ntdll` or `kernel32`
survives an audit of the link map.

Delivered 2026-08-30, in `runtime/imports/`. The import list is cut from the
built `cygwin1.dll` DR-0007 names -- Cygwin 3.6.10, pinned by SHA-256 in the
reproduce test -- with `objdump -p`, since the imports live in the binary's
import table and not in `cygwin.din`. `extract-imports.sh` writes 370 rows
across two DLLs, 238 from KERNEL32 and 132 from ntdll, and `gen-wrappers.sh`
turns each into one `ms_abi` forwarding thunk in `wrappers.gen.c`. Each thunk
tail-jumps through the import's address slot and repacks nothing, so it is
correct at any arity; DR-0009 records why the System V to MS translation lands
at the caller's site rather than in the wrapper, which is what lets the whole
set be generated without the 370 signatures. `t/reproduce.sh` is the
certification: it pins the DLL, reproduces the inventory and the wrappers byte
for byte, compiles the wrappers and checks every thunk is a frameless tail
jump, and runs `audit-imports.sh`, the link-map audit -- it reads each object's
undefined symbols and fails any object but the wrappers unit that names an
import, and against the wrappers object alone it confirms they are the sole
namer of the 370 slots. No import is variadic, so WP-24 inherits nothing to
unpack from the down-call side; the generator refuses a variadic import rather
than forwarding one, against the day a later base adds one.

### WP-22 — the host-facing core

Needs: WP-21.
Delivers: MS-ABI entry points with SEH unwind data for `DllMain`, thread starts,
APCs, TLS callbacks, vectored exception handlers, and the fault path.
Done when: a fault taken inside the ELF world reaches Cygwin's existing signal
machinery and returns, with the register state on both sides matching what each
convention promises.

Risk: this is the treacherous half. Cygwin's signal delivery rides Windows SEH
and MS-format unwind data, so every path Windows can call into has to be MS-ABI
with unwind information the host recognizes. One missed callback leaks the
convention out the bottom, and the symptom is corruption rather than a link
error.

Partial 2026-08-30, in `runtime/core/`. Certified: the six host-facing entry
points -- `DllMain`, thread start, APC, PE TLS callback, vectored exception
handler, and the signal landing -- as `ms_abi` functions that reach System V
code one frame down and return, each carrying SEH unwind data the host's own
`RtlLookupFunctionEntry` recognizes. The done-condition is met at stand-in
width: a null store beneath a `sysv_abi` frame, and directly in one, reaches
Cygwin as SIGSEGV and returns through `siglongjmp`, and the callee-saved set
each convention promises survives the crossing in both directions, checked
against hand-written poison in `t/probe.S` with leaky controls that light every
bit. DR-0012 records the load-bearing finding spike 3 left open: gcc emits
host-recognized unwind data for `ms_abi` frames and none for `sysv_abi` ones, so
the entry point is the seam and no host unwinder walks a System V frame.
`t/run.sh` builds with the host `x86_64-pc-cygwin` gcc, confirms the `.pdata`
and the `-mno-red-zone` policy (DR-0006), and runs the nine-case crossing test
to verdict yes.

Deferred, because a full runtime does not yet exist: `DllMain` fired by the
loader as a linked DLL's entry and the PE TLS callback fired from the DLL's TLS
directory are WP-41's, so only their shape, convention and unwind are certified
here; the System V to MS trampolines for pointers the ELF world hands down are
WP-23's, and their seam is marked in `core.h` rather than filled; the signal
frame's `siginfo_t`/`ucontext_t` layout and the red-zone reservation at the
delivery site are WP-43's. The entry-point bodies are stand-ins that make the
crossing observable, not the runtime work they will front.

### WP-23 — the callback trampolines

Needs: WP-22.
Delivers: System V to MS trampolines for anything the ELF world hands down to
Windows as a function pointer.
Done when: a `qsort` comparator, a thread start routine, and an exception filter
each survive a round trip with their callee-saved registers intact.

### WP-24 — varargs

Needs: WP-21.
Delivers: deliberate handling for the variadic surface, `printf` family
included, rather than the generated wrapper the fixed-arity calls get.
Done when: a `printf` call with sixteen mixed integer and floating arguments
prints what Linux prints, and `vfprintf` called through a `va_list` built on the
System V side works from the MS-ABI side.
Risk: there is no forwarding to be had. Spike 3 measured the two `va_list`
types at twenty-four bytes and eight, and handing the System V one to a reader
shaped for Microsoft's fetched the pair of offsets at its head as the first
argument. Every variadic export walks its own list with
`__builtin_sysv_va_start` and passes the values on one at a time, so this
package is real work rather than a wrapper.

### WP-25 — the compatibility counter

Needs: WP-22.
Delivers: API major and minor counters for `elfsysv1.dll`, a changelog
discipline, and the runtime check that reads what a program was built against.
Done when: a program built against a lower minor runs against a higher one, and
the reverse is refused with a diagnostic rather than a crash.

Cygwin's rule is inherited down to the digit in the name, so the counter starts
at the first release rather than at the first break. Retrofitting one after the
fact means guessing which of the existing binaries predate which change.

---

## Phase 3 — TLS and the loader

### WP-30 — the thread pointer

Needs: DR-0003 (settled), WP-22.
Delivers: thread pointer establishment at thread creation and wherever the host
can disturb it, with the TCB in the psABI's variant II layout.
Done when: a thread reads its own TCB correctly after a hundred thousand
context switches under load, and after a `fork`, and after a signal delivered
mid-computation.

Risk: this package waited on spike 1 and then on a person, and both have
answered. Spike 1 took `%fs` away on 2026-08-29; DR-0003 settled the
replacement the same day as carrier C3 of `spike/gs-thread-pointer/`, a
runtime-owned thread pointer kept below the stack base and reached through
`%gs`. The interface never changed — a thread pointer, established and readable,
is the same contract whatever produces it — and the body may now be written
against C3. The acceptance test above is stated in terms of the TCB rather than
the FS base, so it already fits the carrier and its own persistence cases the
spike ran. The carried risk is DR-0003's: the spike measured a stand-in, and
this package re-measures the real `_my_tls` as it builds against it.

### WP-31 — ELF parsing

Needs: nothing.
Delivers: header, program header, and `PT_DYNAMIC` parsing with bounds checking
on every table reached through them.
Done when: the fuzz target in WP-T1 runs a hundred million cases without a
fault, and every rejection carries a diagnostic naming the field.

This package is first in the loader because it is the only one that reads
attacker-shaped input from its first line. Everything after it may assume its
output is structurally sound, and that assumption is worth buying properly.

Delivered 2026-08-30, under `loader/elf/`. The parser is `elf_parse.c` with its
own ELF definitions in `elf_types.h`, producing the validated view described in
`elf_parse.h` and `loader/elf/README.md`: header, program headers, and the
dynamic section, with every table reached through them translated to a
bounds-checked file offset and every version chain walked in full for loops and
range. The done-when target was met on the pinned RHEL-8.10 toolchain, where
the WP-T1 fuzz target ran a hundred million cases in one run (100,000,000
cases, 4,348,839 accepted and 95,651,161 rejected, in about seven minutes)
without a crash, undefined behaviour, a rejection missing its field, or an
acceptance that violated the invariants. That toolchain's gcc 7.4 ships no
AddressSanitizer runtime, so memory safety was enforced with a guard page after
each image and with
`-fsanitize=undefined -fsanitize-undefined-trap-on-error`, which needs no
runtime library; `loader/elf/README.md` records the substitution.

### WP-32 — segment mapping

Needs: WP-31.
Delivers: `PT_LOAD` placement through Cygwin's `mmap` by reserve-and-commit,
one region per object, with `PT_GNU_RELRO` and `PT_GNU_STACK` honored.
Done when: a static ELF maps and runs, and every mapping the loader made is
visible to Cygwin's own bookkeeping, which is what WP-41 depends on entirely.

Windows reserves at 64 KB granularity and ELF aligns segments at 4 KB, so the
arithmetic is the interesting part; cross-process text sharing is the price.
Spike 2 did that arithmetic on 2026-08-29 and it holds at both alignments,
including the 2 MB one. It also settled three details this package no longer
has to discover. Freshly committed pages arrive zeroed, so `.bss` is free. A
link base that is not granule-aligned costs the bytes between it and the
granule below and nothing else. And the protections have to be applied in a
second pass after every segment is copied, because two segments can share a
page and a segment made read-only before its neighbour is filled makes the
fill fault.

What spike 2 did not settle, and what still costs this package, is the address
rather than the arithmetic: at a 2 MB `p_align` a three-segment image spans
4 MB, and 4 MB at `0x400000` was unavailable in a Cygwin process every time it
was asked. That is WP-41's ordering problem rather than this one's, but the
span this package computes is what makes it bite. Whether el8 binaries carry
the linker's 2 MB `max-page-size` default therefore decides how often it
bites, and one `readelf` against a vendor binary still settles it.

Delivered 2026-08-30, under `loader/map/`. `elf_map.c` reserves and commits the
object's whole span as one region, copies each `PT_LOAD` to its runtime
address, confirms the `.bss` tail arrived zeroed rather than assuming it, and
applies protections in a second pass; `host_mem.c` reads the page size and
allocation granularity from the host so nothing is hardcoded. `elf_map.h`
carries the contract, `elf_map_protect_relro` is the hook WP-34 calls once it
has written through the relro range, and `PT_GNU_STACK` is recorded for WP-40.
The done-when target was met: the certification in `loader/map/t/` builds three
static ELF specimens with the cross toolchain and maps each, reading
`/proc/self/maps` to prove the runtime recorded every segment — the visibility
WP-41 depends on — touching the pages with fault probes to prove the
protections are real, and entering the image to prove it runs with `.bss` zero.
The 64 KB-aligned and 2 MB-aligned specimens both map and run; a 4 KB-aligned
one whose segments share a host granule and a second placement over an occupied
span are both refused.

The change the plan did not anticipate is that the placement goes through the
runtime's `mmap` rather than `VirtualAlloc`, because a `fork` replays only what
that bookkeeping recorded, and the host's mmap semantics then shape the package:
there is no reserve-then-commit split to sub-commit into, so the span is one
committed region, and a protection change snaps to the allocation granule, so an
object placing two segments of unlike protection inside one granule cannot be
honored and is refused. Both the arithmetic and this constraint were measured
before the code was written and are recorded in
`doc/decisions/0008-mmap-granule-protection.md`. The low-address ordering
problem is sidestepped by mapping at a high base and left to WP-41 as the plan
intended.

### WP-33 — the object graph

Needs: WP-32.
Delivers: `DT_NEEDED` walked breadth-first, `DT_SONAME` as identity, `DT_RPATH`
and `DT_RUNPATH` with their precedence difference, `LD_LIBRARY_PATH`, the
system path, and `ldconfig` with a cache format.
Done when: `ldd` on a vendor binary lists the same objects in the same order a
real `ld.so` lists them.

### WP-34 — relocation

Needs: WP-33.
Delivers: the `R_X86_64_*` set el8 objects actually contain, `RELA` throughout,
`RELR` where the toolchain emits it, `IRELATIVE` with its resolvers, lazy PLT
binding and `BIND_NOW`.
Done when: a dynamically linked hello runs both ways, and an ifunc-dispatched
`memcpy` selects the same implementation a real loader selects.

### WP-35 — symbol lookup

Needs: WP-34.
Delivers: GNU hash and SysV hash, scope ordering, `RTLD_GLOBAL` promotion, and
the interposition rules behind `LD_PRELOAD`.
Done when: the differential test in WP-T2 agrees with a real `ld.so` on
resolution order for a graph with a deliberate three-way name collision.

### WP-36 — the version matcher

Needs: WP-35.
Delivers: `.gnu.version`, `.gnu.version_d`, and `.gnu.version_r` read and
matched; default `@@` and non-default `@` bindings distinguished; parent chains
walked; a missing non-weak requirement refused at load with the message a real
loader gives.
Done when: a consumer built against `GLIBC_2.14`'s `memcpy` binds to that body
and not to the `GLIBC_2.2.5` one in the same library, and removing the node from
the library makes the load fail rather than silently pick the survivor.

This is the package the whole project exists for, it is a few hundred lines, and
it is written from Drepper's specification because glibc's implementation is
LGPL-2.1-or-later and assumes a kernel we do not have. Copying it would make
this a derivative either way, so the reason to write from the specification
stands; the licence is named correctly here because DR-0004 turns on the
difference. Budget review time rather than coding time.

### WP-37 — TLS in the loader

Needs: WP-30, WP-34.
Delivers: static block sizing from the initial `PT_TLS` set with a documented
surplus for late arrivals, the dtv, `__tls_get_addr`, TLS descriptors if the
toolchain emits them, and teardown that releases per-module blocks.
Done when: all four TLS models resolve correctly in one program, and a
`dlopen`-ed module with its own `PT_TLS` works in a thread created before the
`dlopen`.

Risk: the surplus is a tunable with a default, not a constant. Getting it wrong
fails at run time inside a library the program did not know it would load.

### WP-38 — the dl surface

Needs: WP-36, WP-37.
Delivers: `dlopen`, `dlsym`, `dlvsym`, `dlclose`, `dlerror`, `dladdr`,
`dladdr1`, `dlinfo`, `dl_iterate_phdr`, and initialization order
(`DT_PREINIT_ARRAY`, dependencies before dependents, `DT_INIT` and
`DT_INIT_ARRAY`, the reverse on the way out), with a defined cycle tie-break.
Done when: a plugin loaded and unloaded ten thousand times leaks nothing, and
the unwinder finds `.eh_frame` through `dl_iterate_phdr` for an object that
arrived after startup.

### WP-39 — the rendezvous

Needs: WP-33.
Delivers: the SVr4 `r_debug` structure, a link map kept current, and the
breakpoint function a debugger sets.
Done when: a gdb built for the triple lists every loaded object and sets a
breakpoint in one that arrived through `dlopen`.

Early rather than late, and deliberately so. The alternative is debugging a
world no tool can see, which taxes every package after this one.

---

## Phase 4 — process integration

### WP-40 — the initial process image

Needs: WP-32.
Delivers: the stack built downward (`argc`, `argv`, null, `envp`, null, auxv,
strings, `AT_RANDOM` bytes), entry reached with `%rsp` on `argc` and the ABI's
alignment honored.
Done when: the auxv a real Linux kernel builds and the auxv we build differ only
in the entries that describe the platform, and `AT_SYSINFO_EHDR`'s absence is
tolerated by every consumer in the set.

There is no vDSO here, so anything that would have gone through one goes through
the runtime. A consumer treating the missing entry as fatal is a bug worth
finding in this package rather than in month nine.

### WP-41 — exec dispatch and the stub

Needs: WP-38, WP-40.
Delivers: the magic-byte branch in Cygwin's spawn path, and the PE host stub
that reserves the address space, opts out of CET shadow stacks and Control Flow
Guard, loads the runtime, and hands the loader the file and the argument vector.
Done when: `execve` on an ELF binary from a Cygwin program works, `#!` scripts
still work, the ordering between the ELF, `#!`, and PE cases is written down,
and the interpreter recursion limit is enforced.

Descriptor inheritance, close-on-exec, the environment, the working directory,
signal disposition reset, and `AT_SECURE` all come with it. Cygwin's `execve`
never could replace a process image, so the parent-stub fiction is inherited
rather than introduced.

Risk, and it is spike 2's: the reservation has to come first, before anything
else in the process allocates without a base. Windows satisfies a
based-anywhere allocation out of the lowest free region, so a runtime that has
already started has already taken part of where a non-PIE image expects to
live. Measured 2026-08-29: a 4 MB span at `0x400000`, which is what a
three-segment image with a 2 MB `p_align` asks for, was refused twenty times in
twenty inside a Cygwin process, while the same image mapped at `0x10000000`
without complaint. Reserving from a PE TLS callback or the image entry point,
ahead of the runtime's own initialization, is where this points, and whether
that is early enough is unmeasured. Measure it at the start of this package
rather than discovering it in the middle.

### WP-42 — fork

Needs: WP-41.
Delivers: loader state crossing into the child intact — object list, search
paths, TLS block and every dtv, the `r_debug` structure and its address — plus
`pthread_atfork` ordering and the loader lock held across the call and released
on both sides.
Done when: a `fork` from a thread that is inside `dlopen` produces a child that
runs rather than a child that deadlocks, and the DLL rebase failure mode that
haunts Cygwin's `fork` is confirmed absent rather than assumed absent.

`posix_spawn` and `vfork` take the same path and get the same test.

### WP-43 — signals

Needs: WP-42, WP-23.
Delivers: the trampoline from Cygwin's thread-hijack delivery onto an ELF-side
stack, with `siginfo_t` and `ucontext_t` laid out as the psABI and the Linux
headers agree, extended FPU state saved where a consumer looks for it, and a
return path that restores all of it.
Done when: `sigaltstack` works, `SA_SIGINFO` and `SA_RESTART` mean what they
mean, a signal delivered to a thread inside the runtime returns correctly, and
the 128 bytes below `%rsp` are intact on return for compiled code.

Risk: the red zone is a specification guarantee this package breaks, and spike 3
showed it is this package rather than the host. Windows leaves the reserved 128
bytes alone; Cygwin's hijack delivery builds the handler's frame at the
interrupted stack pointer and takes `%rsp-8` first. `-mno-red-zone` closes that
only for code the compiler emitted, so hand-written assembly stays exposed,
which is why WP-16 delivers a ledger. The alternative -- this trampoline
skipping 128 bytes before it builds anything, which would close it everywhere
at once -- is no longer a question of which. DR-0006 settled it as the
destination on 2026-08-29 and left this package the price: measure the
reserving delivery against Cygwin's real `sigdelayed` rather than spike 7's
model, read the number against DR-0006's bands, and write the record that
retires the flag or reopens the direction.

---

## Phase 5 — the veneer

### WP-50 — the headers

Needs: WP-10.
Delivers: a glibc-shaped header set, `features.h` included, with `__GLIBC__` and
`__GLIBC_MINOR__` reporting el8's numbers and the feature-test macro behavior
that goes with them.
Done when: a package that probes the headers and then links the library gets one
answer rather than two.

First in this phase and early in the program overall, because WP-14 needs it and
because a header set can be written before anything implements it.

Delivered 2026-08-30 in `veneer/include/`, the header half of the exit
criterion. The half that needs a library to link is WP-53's; the half that does
not is the feature-test-macro plumbing, and it is certified. `features.h`
reproduces el8's glibc 2.28 arithmetic verbatim (DR-0010) and `veneer/t/ftm-diff`
diffs its `__USE_*` resolution against the vendor header
(`glibc-headers-2.28-251.el8_10.40`, fetched and pinned per DR-0002) over 43
input sets — the default, strict-ANSI at each C standard, every POSIX and X/Open
level, the ISO C source macros, the large-file and at-file switches, the
reentrancy synonyms, the deprecated BSD/SVID aliases, the fortify levels, and
C++ at three standards. All 43 match, with `__GLIBC__` 2 and `__GLIBC_MINOR__`
28. The WP-14 draft of the same three files in `toolchain/sysroot/include/` is
superseded; it diverged on the untested combinations (DR-0010 lists them), which
is what a paraphrase does and what this delivery replaces with a copy.

Deferred: the rest of the C library header surface, `gnu/stubs.h` content
(WP-52's), and rewiring the toolchain sysroot to consume `veneer/include/`
(a WP-14/WP-15 follow-up).

### WP-51 — the version map

Needs: WP-20, WP-50.
Delivers: every node from `GLIBC_2.2.5` through `GLIBC_2.28` with every symbol
at the node el8's own glibc put it at, extracted from vendor binaries and
committed as a generated artifact.
Done when: the extraction reruns and reproduces the committed map, and the node
set matches what `readelf -V` prints for the vendor's `libc.so.6`.

Several thousand symbol-to-node bindings maintained by hand would be wrong
within a month, so this is generated or it is not trustworthy.

### WP-52 — the resolution classification

Needs: WP-51.
Delivers: every symbol in the map sorted into one of four buckets — forwards to
a runtime export under another name, forwards under the same name, needs a shim
because the semantics differ, or has nothing behind it and becomes a stub that
fails predictably.
Done when: the four buckets partition the map with no symbol unclassified, and
the fourth bucket is published rather than filed.

That fourth bucket is the honest inventory of what this platform does not have.
It is the most useful document the project will produce for anyone deciding
whether to depend on it.

### WP-53 — libc.so.6

Needs: WP-52, WP-36.
Delivers: the ELF `libc.so.6` itself, versioned aliases resolving into
`elfsysv1.dll`, plus the static side (`libc.a` and the startup files, which
WP-14 consumed in draft).
Done when: `memcpy@GLIBC_2.2.5` and `memcpy@@GLIBC_2.14` both live in the object
and bind independently, and spike 4's `elfdeps` result reproduces against the
real library rather than against the synthesized one.

### WP-54 — the companion libraries

Needs: WP-53.
Delivers: `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`,
`libcrypt.so.1`, `libresolv.so.2`, `libnsl.so.1`, `libutil.so.1`.
Done when: a vendor binary's `DT_NEEDED` list is satisfied entirely from our
tree with no name left over.

el8 still ships these as separate objects rather than the merged glibc of later
releases, so the partition follows el8's rather than a modern one.

---

## Phase 6 — packaging and tooling

### WP-60 — gdb for the triple

Needs: WP-39, WP-15.
Delivers: a gdb configured for the target, consuming `r_debug`.
Done when: it breaks, steps, and prints locals in an ELF program running under
the stub, and unwinds a C++ exception across a shared library boundary.

### WP-61 — core dumps

Needs: WP-60.
Delivers: a decision, then whatever the decision implies.
Done when: either an ELF core our gdb reads is produced on a fault, or the
tree documents that a Windows minidump of a stub is what a crash leaves behind
and says how to work from one.

Open rather than planned, and it can wait. Not indefinitely, since the first
hard crash in a package build is when somebody wants it.

### WP-62 — the rpm surface

Needs: WP-54.
Delivers: confirmation rather than code — `file` reporting ELF, rpm's magic gate
matching, `elfdeps` firing unmodified — plus `ldconfig`, the cache format, and
the search path configuration it reads.
Done when: a built package's `Provides` and `Requires` carry
`libc.so.6(GLIBC_2.2.5)(64bit)` in the vendor's exact shape.

Most of this repairs itself the moment the format is ELF. The stage 0.5
admission in `symbol-versioning-formats.md` for a PE dependency generator does
not apply on this route, and that admission should be marked superseded rather
than left to confuse a later reader.

### WP-63 — installation

Needs: WP-62.
Delivers: the installer and configurator, idempotent per `AGENTS.md`: derived
configuration reseeded from a pristine template each run, orphans from retired
revisions removed rather than merely untouched, leftovers reaped before acting.
Done when: two runs with changed inputs leave only the new state, with injected
stale values cleared and retired items gone.

Endpoint protection exclusions belong here too. Self-mapped anonymous
executable memory is malware-shaped, permanently, so the exclusion is a
documented deployment step rather than a defect anyone will fix.

---

## Cross-cutting — test infrastructure

These start with WP-31 and run beside everything after it. They are not a phase
and they do not finish.

### WP-T1 — fixtures and fuzzing

Needs: WP-31.
Delivers: a committed fixture corpus, small and ugly on purpose — zero-length
segments, overlapping `PT_LOAD`, a `DT_NEEDED` pointing past the end of the
string table, a verneed chain that loops — and a fuzz target fed malformed and
truncated ELF.
Done when: it runs in CI on the pinned 2019 Cygwin and a new crash blocks a
merge.

A relocator that has never seen a truncated `PT_DYNAMIC` is not finished, and
that sentence is a rule rather than a sentiment.

### WP-T2 — differential tests against Linux

Needs: WP-35.
Delivers: comparisons against a real glibc for everything with a specified
answer — TLS layout, auxv contents, the `r_debug` structure, symbol resolution
order, and the version matcher's verdict on a library a real `ld.so` also has an
opinion about.
Done when: each comparison either matches or carries a recorded, justified
divergence.

Checking against a running glibc beats checking against our reading of the
specification, and the two disagree often enough to be worth the harness.

### WP-T3 — spike transcript regeneration

Needs: any spike.
Delivers: a runner that reruns every spike script and diffs against its recorded
transcript.
Done when: a spike whose script has rotted fails the same way a broken unit test
fails.

### WP-T4 — the acceptance comparison

Needs: WP-62.
Delivers: the harness that takes a vendor source package, builds it against this
tree, and compares the result against what Red Hat shipped.
Done when: it runs unattended over the el8 set and produces a per-package
verdict.

This one belongs to `rhelcyg-8.10` rather than here, and it is listed because
it is the criterion everything above serves. A tree that passes every test in
WP-T1 through WP-T3 and fails this one has not done the job.

---

## The graph, condensed

    TLS model ───────────────────────► WP-30 ─┐
    WP-10 ─► WP-11 ─► WP-12 ─► WP-13 ─► WP-14 ─► WP-15 ─► WP-16
       └───► WP-50 ─────────────────────┘
    WP-20 ─► WP-21 ─► WP-22 ─► WP-23 ─► WP-43
                        └────► WP-24, WP-25
    WP-31 ─► WP-32 ─► WP-33 ─► WP-34 ─► WP-35 ─► WP-36 ─► WP-38
                        └────► WP-39          WP-37 ─────┘
    WP-32 ─► WP-40 ─► WP-41 ─► WP-42 ─► WP-43
    WP-51 ─► WP-52 ─► WP-53 ─► WP-54 ─► WP-62 ─► WP-63

The spikes have all answered and the three reserved decisions are settled, so
the graph waits on no input outside itself; the `TLS model` node above is
DR-0003, and WP-30 builds against it. Three chains run genuinely in parallel
from here: the
toolchain, the runtime, and the loader. They meet at WP-41, which is the first
package that needs all three, and the program's critical path runs through
whichever of them is slowest rather than through any one of them by name.

WP-36 is the shortest package on the critical path and the one that justifies
the program. WP-22 is the longest single risk. WP-53 is where anyone outside
the project first sees the point.

## Not verified

The runtime face at a DLL's width. Spike 3 ran on 2026-08-29 and answered yes,
so phase 2's premise is measured rather than assumed, but it was measured at one
function. Unwind data crossing a `sysv_abi` frame, `DllMain`, and PE TLS
callbacks into a System V-faced DLL are what phase 2 meets first and what the
spike never touched; `spike/abi-crossing/README.md` lists them.

The other two decisions have answers: DR-0001 fixed the triple and spike 5
priced it on 2026-08-29 at one affected package in 2893, and spike 1 refuted
`%fs`-relative TLS the same day. What replaces `%fs` is settled too: DR-0003
picked carrier C3 of `spike/gs-thread-pointer/`, a runtime-owned thread pointer
through `%gs`. The one thing still to verify there is not the model but the
block: the spike measured a stand-in for Cygwin's `_my_tls`, and WP-30
re-measures the real one as it builds against it.

That Cygwin's `fork` replays every mapping made through its own `mmap`. WP-42 is
built entirely on it and it is asserted from the design of both rather than
measured.

That el8 binaries carry 2 MB `PT_LOAD` alignment. Settled on 2026-08-29 and
the answer is yes, uniformly: every one of the eighty-two `PT_LOAD` segments
in `spike/vendor-image-shape/` has `p_align` 0x200000. So WP-41's ordering
constraint bites in the common case rather than the awkward one. The same run
found no non-PIE image at all, which would take the constraint away entirely
if it held across the set, and three packages is not the set.

That a PE TLS callback or an image entry point runs early enough to reserve a
non-PIE image's span before the runtime takes part of it. Spike 2 established
that something has to, and named these as where to look; neither has been
tried. WP-41 rests on one of them working.

The size of the WP-51 map. Several thousand bindings is an estimate from the
shape of glibc's export list, not a count.

That el8's rpm 4.14.3 carries `elfdeps` and `fileattrs`. Checked on
2026-08-29: `rpm-build-4.14.3-32.el8_10` ships both, `elf.attr` among the ten
fileattrs, so the omission WP-62 attributes to Cygwin's port does belong to
the port. `spike/vendor-image-shape/` has the transcript.
