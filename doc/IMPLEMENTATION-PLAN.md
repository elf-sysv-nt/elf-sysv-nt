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

`milestones.md` is the governing document for the spikes and it is not restated
here. What belongs in a plan is the rule around them: run one through to its
stated verdict without asking, then stop at the boundary and report, rather than
beginning the work the answer implies.

The five gating spikes are done, all on 2026-08-29, with six more run since and
three planned above. Spike 3 did not want spike 2's stub in the end: the crossing is measurable inside one process with two hand-written
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

### Planned spikes (F4)

Three censuses are cut but not yet run. Their bands are written before the
numbers exist, per the DR-0001 discipline, and are recorded in full in
`doc/design-gaps/proposal.md`. They run on one shared el8 corpus fetch and land
as `milestones.md` rows when they run; because they need that fetch and a
judgement on the bands, they are run deliberately rather than swept up by the
autonomous worker.

Spike 12, the demand census: how many el8 packages need a symbol the
classification cannot yet stand behind, and — as the useful by-product — the
demand ranking WP-56's slices are ordered by. Bands: under 10% of packages
touching bucket 4 is the tail already planned; 10% to 40% is WP-56 proceeding
with a published compatibility statement; over 40% is a program-level review.

Spike 13, the site census: over the same corpus, the shares of `%fs` TLS sites
that are read-modify-write, that carry a `lock` prefix, and that are the
self-pointer form, and the count of raw `syscall` instructions outside glibc's
own objects — the count proposal 0003 declared no longer optional, and the price
on DR-0005's raw-syscall bound.

Spike 14, build throughput: one mid-size autotools package built end to end
under the pinned root against the same build on el8, timed, with fork count and
peak commit recorded beside the wall clock. It gates whether a performance pass
is owed before the mass rebuild rather than during it.

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

Follow-up (F8): the compiler's default output is to be made PIE — `-pie` by
`TARGET_SUBTARGET_DEFAULT` alongside `-mno-red-zone`, so `ET_DYN` is the default
shape el8's toolchain gives (`doc/target-definition.md`, the PIE default). It
lands when the toolchain is next built at WP-26, since a one-line specs default
does not warrant rebuilding a closed stage on its own, and `t/accept.sh` gains
the claim then.

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

Completion (F4): the `asm-ledger` run over the el8 set is scheduled with spike 13,
which fetches the same corpus; the two run together, and the ledger they produce
is the named residue of both the red-zone flag and DR-0012's unwind rule.

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

Delivered 2026-08-30. `runtime/core/t/run.sh` verdict yes, nine cases,
zero failures; the deferred items above remain with WP-23, WP-41 and WP-43.

### WP-23 — the callback trampolines

Needs: WP-22.
Delivers: System V to MS trampolines for anything the ELF world hands down to
Windows as a function pointer.
Done when: a `qsort` comparator, a thread start routine, and an exception filter
each survive a round trip with their callee-saved registers intact.

Delivered 2026-08-30, in `runtime/core/`, filling the `ELFSYSV_WP23_SEAM` WP-22
marked. `callback.c` carries one `ms_abi` trampoline per callback shape, each
reaching its System V target through a mutable slot; from the slot's `sysv_abi`
type the compiler emits the Microsoft-to-System-V crossing at the call -- the
argument shuffle and the save and restore of `%rsi`, `%rdi` and `%xmm6`-`%xmm15`,
the set a Microsoft caller keeps and a System V callee scratches, the direction
spike 3 named as the one the design was nervous about. The generated prologue
carries the `.seh_` records the host recognizes, so the trampoline is the frame
the host's unwinder walks to and stops at, the role DR-0012 reserves for it.
`t/callback-run.sh` builds with the host `x86_64-pc-cygwin` gcc, confirms the
`.pdata` and the `-mno-red-zone` policy, and runs the crossing test: a
comparator, a thread start routine and an exception filter each survive a round
trip called Microsoft x64 with the full Microsoft callee-saved set intact and
their arguments delivered, checked against hand-written poison in
`t/callback_probe.S`; a de-bracketed trampoline leaks exactly `%rsi`, `%rdi` and
`%xmm6`-`%xmm15` and a total-leak callee lights every bit, so the check is known
able to fail. DR-0020 records that the trampolines are a fixed compiled set with
one live target per shape and no run-time code generation, which DR-0000
forecloses.

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

Delivered 2026-08-30, in `runtime/varargs/`. Each variadic entry walks its own
System V `va_list` and rebuilds a Microsoft one, then repasses through the
core's `va_list`-taking form -- there is no `...` callee to splat a run-time
list into, so the repass targets `vfprintf`/`vsnprintf`/`vfscanf`, named in
`core.h`, which DR-0015 records. For the `printf` and `scanf` families the
rebuild is driven by the format, in `sv2ms.c`, because only the format recovers
whether an argument came from the integer or the floating register file; the
prototype-driven variadics (exec, `open`, `fcntl`, the IPC ctls) walk their
fixed signatures instead. `variadic-exports.tsv` enumerates the surface -- 68
exports, 54 format-driven and 14 prototype-driven, derived by signature and
certified against WP-20's inventory since the `.din` does not mark them -- and
`gen-veneer.sh` writes one `sysv_abi` entry per format-driven export from it.
`t/reproduce.sh` is the certification: the set is consistent, the veneer
reproduces byte for byte, the bridge and entries compile clean, and the
runnable test passes -- a sixteen-argument `printf` reproducing glibc 2.35's
output exactly, and a `vfprintf` reached through a System V `va_list` formatting
through a Microsoft-ABI callee.

### WP-25 — the compatibility counter

Needs: WP-22.
Delivers: API major and minor counters for `elfsysv1.dll`, a changelog
discipline, and the runtime check that reads what a program was built against.
Done when: a program built against a lower minor runs against a higher one, and
the reverse is refused with a diagnostic rather than a crash.

Cygwin's rule is inherited down to the digit in the name, so the counter starts
at the first release rather than at the first break. Retrofitting one after the
fact means guessing which of the existing binaries predate which change.

Delivered 2026-08-30, under `runtime/version/`. The counters, the stamp a
program carries, and the runtime's own stamp are in `elfsysv-version.h`, re-facing
Cygwin's `CYGWIN_VERSION_API_MAJOR`/`_MINOR`, `CYGWIN_VERSION_DLL_IDENTIFIER`,
and the `per_process` fields its crt0 writes, read against `newlib-cygwin`
b11613e47. The load check is `compat.c`/`compat.h`, re-facing
`check_sanity_and_sync` (`dcrt0.cc`) in its order — the stamp-size backup, the
generation digit, then the counter — and returning a verdict with a diagnostic
rather than aborting, so the caller reports and exits cleanly. Two departures
from Cygwin are recorded in DR-0018: the refusal reads the combined
`major * 1000 + minor` rather than the major alone, because every additive change
lands in the minor and a program built after one needs a runtime that has it; and
the pair starts at `0.1`, the first release, with `CHANGELOG.md` as the changelog
discipline and the `0.1` baseline. The done-condition was met on the host
toolchain, where `t/run.sh` builds and runs `t/compat_test.c`: a program stamped
`0.3` runs against a runtime stamped `0.5`, a program stamped `0.7` is refused
against it with a diagnostic naming both versions, and the equal, major-axis,
wrong-generation, and stamp-size cases each reach their expected verdict, eleven
cases with no failure. The counter is built and run on the host gcc, as WP-22's
crossing was, because the verdict has to run; `compat.c` is plain integer and
string comparison and links into the runtime unchanged, and cross-compiles once
the veneer headers (WP-50) are in the cross sysroot, which `README.md` records.

### WP-26 — winsup builds as elfsysv1.dll

Needs: WP-21, WP-25, DR-0007.
Delivers: the `newlib-cygwin` tree at `b11613e47`, vendored or fetched per
DR-0002's pattern, compiled `-mno-red-zone` throughout into a DLL named
`elfsysv1.dll` whose face is still Microsoft's — a re-badged Cygwin, not yet
re-faced. The imports route through WP-21's generated wrappers, the WP-25
counter is compiled in rather than tested beside, and `_cygtls` gains the
reserved carrier field DR-0021 left to "the forked runtime", its offset
asserted against `sizeof(_cygtls)` at build time.
Done when: a hello built against the DLL runs; the build reproduces from the
pinned ref byte-for-byte in the parts the toolchain makes reproducible; and
`runtime/tls/measure/` reruns against the forked block, closing the DR-0003
re-measurement chain against the real 3.6.10 `_cygtls` rather than 3.0.7's.
Risk: this is the first time Cygwin's source is compiled rather than called,
the last unreached item on spike 3's list. Expect it to find things, and expect
the `-mno-red-zone` world to surface hand-written assembly inside winsup itself;
that residue joins WP-16's ledger.

### WP-27 — the System V face at the DLL's width

Needs: WP-26, WP-22, WP-23, WP-24.
Delivers: the export surface re-faced per WP-20's inventory — System V outward,
the variadic entries from WP-24's generated veneer, the host-facing entry points
in WP-22's certified shapes now fronting the real runtime work they were
stand-ins for. Thread creation establishes the carrier for every thread the
runtime creates, which is where the veneer's `pthread_create` inherits it (F8),
and the per-thread split of the blocked mask and alternate stack that DR-0030
deferred lands here with it.
Done when: WP-22's and WP-23's crossing certifications rerun unchanged against
the real DLL; `DllMain` and the PE TLS callback fire from the host's own loader
rather than from a harness; a fault beneath a System V frame still arrives as
SIGSEGV and leaves by `siglongjmp`; and a static ELF through WP-41's branch
calls a real export and returns.
Risk: the unwind seam. DR-0012's tripwire must hold against gcc compiling all of
winsup, not six functions, and a failure there reopens that record on its own
stated terms.

---

## Phase 3 — TLS and the loader

### WP-30 — the thread pointer

Needs: DR-0003 (settled), WP-22.
Delivers: thread pointer establishment at thread creation and wherever the host
can disturb it, with the TCB in the psABI's variant II layout.
Done when: a thread reads its own TCB correctly after a hundred thousand
context switches under load, and after a `fork`, and after a signal delivered
mid-computation.

Delivered 2026-08-30, in `runtime/tls/`. `tp.c` establishes the pointer at
thread creation through carrier C3 — `gs:[NtTib.StackBase]` then a fixed offset
into a stack the runtime allocates and owns — and the TCB is the psABI variant
II shape asserted at compile time. `t/tls_test.c` reads a distinct TCB back
through the carrier after the kernel's own count of context switches clears a
hundred thousand with zero mismatches across two threads per processor, after a
`fork`, and after a signal delivered mid-computation on both the thread's own
stack and an alternate signal stack. `measure/` is the re-measurement DR-0003
required: `CYGTLS_PADSIZE` is `0x3200` not the stand-in's page, the reservation
near `StackBase` is Cygwin's live signal state, and the alternate signal stack
leaves `NtTib.StackBase` unmoved so the chain holds through delivery. DR-0021
records the placement that follows — the carrier is the floor of a runtime-owned
stack, not a blind offset below `StackBase` — without reopening DR-0003.
Deferred to WP-37: sizing the static block from `PT_TLS`, the dtv,
`__tls_get_addr`, and teardown. Deferred to the forked `elfsysv1.dll`: moving
the carrier into a reserved field of the runtime's own `_cygtls`, the offset
then measured against `sizeof(_cygtls)`.

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

Delivered 2026-08-30, under `loader/graph/`. `elf_graph.c` walks the graph
breadth-first, `DT_SONAME` as identity, resolving each `DT_NEEDED` against
`DT_RPATH`, `LD_LIBRARY_PATH`, `DT_RUNPATH`, the cache, and the default
directories in that order — with `DT_RPATH` searched before `LD_LIBRARY_PATH`
and inherited down the loader chain while `DT_RUNPATH` is searched after it and
not inherited, the two differences that fall out of the one rule that an object
carrying a runpath contributes no rpath to its dependents. `DT_RPATH` and
`DT_RUNPATH` are read back out of WP-31's already-validated dynamic array rather
than by changing the parser. `ldso_cache.c` is the cache reader and builder, its
format this project's own (DR-0011), and `ldconfig.c` and `elf_ldd.c` are the
tools. The done-when was met differently than its wording anticipated: rather
than one vendor binary, the certification in `loader/graph/t/` builds a battery
of graphs with the cross toolchain — a diamond, an RPATH-versus-`LD_LIBRARY_PATH`
precedence pair, a `DT_RUNPATH` inheritance pair, an `$ORIGIN` lookup, a
cache-only name, and a missing dependency — and compares `elf-ldd`'s order
against a real glibc `ld.so` (Ubuntu's 2.43, through WSL) over every one; all
seven match object for object, and a unit test asserts the internals a
differential cannot see. The comparison `ld.so` is newer than el8's 2.28 and a
real vendor closure is left to the acceptance harness; `loader/graph/README.md`
records both limits.

### WP-34 — relocation

Needs: WP-33.
Delivers: the `R_X86_64_*` set el8 objects actually contain, `RELA` throughout,
`RELR` where the toolchain emits it, `IRELATIVE` with its resolvers, lazy PLT
binding and `BIND_NOW`.
Done when: a dynamically linked hello runs both ways, and an ifunc-dispatched
`memcpy` selects the same implementation a real loader selects.

Delivered 2026-08-30, under `loader/reloc/`. `elf_reloc.c` reads each mapped
object's dynamic view — string and symbol tables, the hash it sizes the symbol
count from, the `RELA`, `RELR`, and PLT relocation tables, the binding flags —
and writes the computed values back through the image; `elf_reloc.h` carries the
contract and `reloc_resolve.S` is the lazy resolver. The relocation set was
measured against the pinned el8 glibc and companions rather than assumed: the
engine computes `RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`, `IRELATIVE`, `TPOFF64`,
`64`, `COPY`, and `DTPMOD64` (the types those objects contain), plus `DTPOFF64`
and `RELR`. `RELA` is used throughout, `IRELATIVE` resolvers run last over a
relocated world, and both binding disciplines are carried — `BIND_NOW` resolves
every PLT slot up front, lazy leaves the slot in the PLT until the first call
trips the trampoline. Symbol resolution is the minimum a relocation needs, a
first-definition scan of the scope in load order; the hashed lookup, scope and
interposition rules, and versioning are WP-35 and WP-36, and `loader/reloc/`'s
README draws that line.

The done-when was met over cross-built dynamic specimens with no libc: a PIE
that imports a function and a datum from a companion object, follows an internal
relocated pointer, and calls an ifunc-dispatched `memcpy`. Walked by WP-33,
mapped by WP-32, and relocated here, it runs both ways — linked lazy, its PLT
slot points into the PLT before the run and at the callee after, so the resolver
bound it; linked `BIND_NOW`, at the callee before the run — and the ifunc lands
on the body the CPUID ERMS criterion (one of glibc `memcpy`'s own) selects on
this CPU, the same body a real loader selects. Two of the delivered types the
platform will not build from source are certified against real objects instead,
recorded in DR-0016: `TPOFF64` against the pinned `libc.so.6`, whose eighteen
`TPOFF64` relocations all land at their static-TLS offsets (the toolchain
refuses `%fs` relocations at link, per WP-12 and DR-0003), and `RELR` over a
constructed stream (this binutils packs `RELR` only against a glibc, and el8
carries none). The lazy resolver crosses the System V–to–Microsoft ABI boundary
DR-0000 describes; full TLS execution and the relocation of real glibc's whole
closure are WP-40 and WP-41's.

### WP-35 — symbol lookup

Needs: WP-34.
Delivers: GNU hash and SysV hash, scope ordering, `RTLD_GLOBAL` promotion, and
the interposition rules behind `LD_PRELOAD`.
Done when: the differential test in WP-T2 agrees with a real `ld.so` on
resolution order for a graph with a deliberate three-way name collision.

Delivered 2026-08-30, under `loader/lookup/`. `elf_hash.c` is the two hash
tables and the per-object probe over them -- `.gnu.hash` with its Bloom filter
and stolen-low-bit chain, `.hash` with its bucket chain, a linear fallback, and
the visibility, type, and version filters a candidate must pass. `elf_lookup.c`
is the scope model and the master lookup: a scope is an ordered list, the global
scope is built as main object, then `LD_PRELOAD` interposers, then the
breadth-first closure, and the search runs global, then the reference's local
list, then an `RTLD_GLOBAL` `dlopen` list, applying glibc's observable binding
rule -- the first global or GNU-unique definition wins outright, a weak is a
fallback a later global still overrides, and with no global the first weak wins.
Interposition is a consequence of that order rather than a rule of its own.
Versioned lookup is WP-36 and enters through one seam, the `elf_version_matcher`
WP-35 passes as none; DR-0019 records the load-bearing choices, including keeping
WP-34's minimal scan as the relocation bootstrap rather than rewriting it. The
done-when was met over the deliberate three-way collision: three objects define
`collide()` returning distinct tags, two PIE roots differ only in the order they
name two of them, and the third is reachable only through `LD_PRELOAD`. The
object this loader binds the reference to is held against the object a real glibc
`ld.so` binds it to, read from the loader's own `LD_DEBUG=bindings` report
through WSL's Ubuntu 2.43; over the plain load order, the reversed load order,
and the interposition the two name the same object -- `libone`, `libtwo`, and
`libthree`. A unit test asserts the internals a differential cannot see. The
comparison `ld.so` is newer than el8's 2.28 and a real vendor closure is the
acceptance harness's, the same limit WP-33 carries; `loader/lookup/README.md`
records that and why the binding is read from the loader's report rather than a
freestanding image's exit status.

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

Delivered 2026-08-30 in `loader/version/`. It plugs into WP-35's
`elf_version_matcher` seam and adds the two answers: `elf_version_match` binds a
reference to the definition its version names, preferring the default `@@` over
the non-default `@` and falling a versioned reference back to the unversioned
base but never to a differently-named node; `elf_version_check_needed` refuses a
load on the first absent non-weak requirement with `ld.so`'s exact message and
tolerates an absent weak one. A node's verdaux predecessors are walked, so
`GLIBC_2.28` satisfies a requirement for `GLIBC_2.14`. Written from the generic
ABI and Drepper, not glibc's LGPL resolver (DR-0000, DR-0004); the load-bearing
readings are DR-0023. `loader/version/t/` holds it to fifteen checks over
version tables laid out as an object carries them — the done-condition binding
(`@GLIBC_2.14` over `GLIBC_2.2.5`) and the refusal on a removed node among them.
The unit form suits it because the matcher reads a table's layout rather than a
build, and WP-51 already showed the real vendor tables parse as `readelf -V`
reads them.

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

Delivered 2026-08-30, in `loader/tls/`. `elf_tls.c` sizes the static block from
the initial `PT_TLS` set by the variant II rule WP-34 already uses, so the
offsets it hands to a `TPOFF64` agree, and adds a surplus below the initial
modules — a tunable defaulting to glibc's 1664 bytes — for late arrivals. The
DTV is glibc's shape behind `tcbhead_t.dtv` at head offset `0x08`, and
`__tls_get_addr` resolves general- and local-dynamic, returning a static
module's own address and allocating a dynamic module's block lazily on first
access; a generation counter bumped on every registration is how a thread
created before a `dlopen` learns to grow its vector and resolve the new module.
Teardown frees the per-module dynamic blocks and the DTV. TLS descriptors are
not implemented, because WP-12's binutils refuses them at link, and the
static-offset assignment for a late initial-exec module is left to WP-38's
`dlopen`; DR-0024 records both the surplus default and those boundaries. The
four models and the dlopen-into-a-prior-thread case are certified in
`loader/tls/t/`, the second phase over the live `%gs` carrier.

### WP-38 — the dl surface

Needs: WP-36, WP-37.
Delivers: `dlopen`, `dlsym`, `dlvsym`, `dlclose`, `dlerror`, `dladdr`,
`dladdr1`, `dlinfo`, `dl_iterate_phdr`, and initialization order
(`DT_PREINIT_ARRAY`, dependencies before dependents, `DT_INIT` and
`DT_INIT_ARRAY`, the reverse on the way out), with a defined cycle tie-break.
Done when: a plugin loaded and unloaded ten thousand times leaks nothing, and
the unwinder finds `.eh_frame` through `dl_iterate_phdr` for an object that
arrived after startup.

Delivered 2026-08-30, under `loader/dl/`. `dl.c` holds the object table the
earlier packages hang off and decides what to hand each of them: a `dlopen`
resolves its closure through WP-33, loads it back to front so every leaf is in
before what needs it, places each object above everything already mapped, and
relocates the whole new group in one WP-34 pass. Relocation became incremental
for it — an object is marked applied and skipped on a later pass, which is not
an optimization but a correctness requirement, since RELR adds the bias into
each slot it names and cannot be applied twice. `dl_init.c` is the order: a
post-order walk over the dependency edges seeded in load order, `DT_PREINIT_ARRAY`
then `DT_INIT` then `DT_INIT_ARRAY` with dependencies first, the recorded
reverse on the way out, and a cycle broken at the edge that closes it.
`dl_addr.c` answers what object and symbol an address is in and hands out each
object's mapped program headers, which is the whole of the coupling to
unwinding: `PT_GNU_EH_FRAME` is a program header, so nothing registers anything.

Both halves of the done-when are measured in `loader/dl/t/`. A cross-linked
plugin carrying a constructor, a destructor, an exported function, a relocated
pointer and unwind tables is loaded and unloaded ten thousand times through the
real `dlopen` and `dlclose`, with the file image, the table slot and the
relocation-scope slot checked after every cycle rather than only at the end;
and the unwinder's own walk finds `PT_GNU_EH_FRAME` in that freshly loaded
object at an address whose first byte reads back as the `.eh_frame_hdr`
version. The order itself is certified against a table the case wrote — a
chain, a diamond, a cycle walked repeatedly for the same answer — because an
order inferred from which constructors happened to run is not an order that was
checked. Function pointers into a loaded object carry `sysv_abi`: the loader is
compiled for the host's Microsoft x64 convention and the object it calls into is
not, and a plain pointer there is a silent wrong-register call. The tie-break,
the late module's TLS, and that boundary are recorded in DR-0025.

Per-thread `dlerror` and link-map namespaces are deliberately absent; the first
waits on WP-42's threads and the second is a change to the table rather than to
this interface.

### WP-39 — the rendezvous

Needs: WP-33.
Delivers: the SVr4 `r_debug` structure, a link map kept current, and the
breakpoint function a debugger sets.
Done when: a gdb built for the triple lists every loaded object and sets a
breakpoint in one that arrived through `dlopen`.

Early rather than late, and deliberately so. The alternative is debugging a
world no tool can see, which taxes every package after this one.

Delivered 2026-08-30, under `loader/rdebug/`. `rdebug.c` maintains one
`_r_debug` structure and a doubly linked `link_map` list hung off it, with every
change bracketed by an `r_state` transition and a call to `_dl_debug_state`, the
breakpoint function whose address it publishes in `r_brk`. The rendezvous is
found through the root object's `DT_DEBUG` entry rather than a fixed address
(DR-0022), which the runtime's mapped-anywhere nature requires, and the list is
WP-33's walked graph lifted node for node in load order. The done-when names a
gdb built for the triple, which does not exist yet — that is WP-60 — so the live
check is deferred there and not faked. What is certified in `loader/rdebug/t/`
instead is the whole of what such a gdb consumes: `r_debug` and `link_map` are
held byte-for-byte to the SVr4/gdb layout by `offsetof` and again by a raw-byte
walk through gdb's literal offsets; the list is walkable exactly as `solib-svr4`
walks it; `r_brk` names `_dl_debug_state`; `r_state` and the chain move correctly
through a startup population, a `dlopen` add and a `dlclose` remove; and end to
end, a real program cross-linked against a real shared library is walked by
WP-33, lifted into the rendezvous, and both objects are read back through
`r_map` in load order — the loader announcing a second object. The link-map
shape and the `DT_DEBUG` discovery are recorded in DR-0022.

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

Delivered 2026-08-30, under `loader/process/`. `process_image.c` builds the
stack as pure layout over a caller-owned buffer: the pointer targets at the
top, then the vector with `argc` at a 16-byte-aligned `%rsp`, the auxv emitted
in the kernel's relative order for the entries carried. It makes no host call —
the platform identity arrives through `proc_image_params`, so the same code is
certified against a real Linux auxv with no host in the loop, and only
`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, and `AT_ENTRY` are read from the image
itself. The `%rdx` atexit value is echoed for the entry trampoline to place.

The differential is measured, not argued. `t/dump_auxv.py` reads a real
kernel's auxv from `/proc/self/auxv` under Linux (WSL here), and
`t/differential.sh` holds our key set against it: against a current kernel the
only differences are `AT_SYSINFO_EHDR` (no vDSO, the load-bearing absence),
`AT_MINSIGSTKSZ`, and the two `AT_RSEQ_*` entries, all newer than the target
world, with no key present in ours that the kernel does not emit. The image
test maps a static ELF through WP-32, builds the stack, enters it on the real
psABI stack through a spike-2-style trampoline, and reads `argc`, `argv[0]`,
`envp`, and the auxv back off it, asserting the alignment and `%rdx` contract
as the entered image saw them. `AT_PAGESZ` reports the 4 KB commit granularity
rather than the 64 KB reservation one, recorded in DR-0014.

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

F8, pending measurement: DR-0027 fixed the `#!` buffer size, and confirming it
against the kernel el8 ships — the 4.18 line, backports included — is one
`git show <ref>:include/uapi/linux/binfmts.h` against el8's kernel source when it
is at hand. If `BINPRM_BUF_SIZE` reads 128 there, DR-0027's constant moves to 128
through the record's own stated mechanism and the fuzz corpus gains the 129-byte
case. The el8 kernel source is not on this machine, so the check is recorded
here rather than asserted from memory.

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

Delivered 2026-08-30, under `loader/exec/`. The measurement came first and both
candidates lost. `t/reserve-when.sh` builds one probe four ways: arming from
`main` is refused, which reproduces spike 2; a `.CRT$XLB` callback cannot be
written at all, since Cygwin's runtime supplies no `_tls_used` and the image
therefore carries no TLS directory; and a replacement image entry point — the
earliest instruction the image owns — is refused with the initial thread's stack
sitting at `0x400000`, put there by the kernel before any instruction ran, over
a region `cygwin1.dll` had already begun chewing into small mappings. What is
early enough is the parent: `VirtualAllocEx` into a child created suspended
takes the whole `0x3FC00000` window and it is intact at the child's entry, and
the fifth case is the control for the fourth, since the same reservation against
a child built with the default stack reserve is refused every time. That is
DR-0028, and it is why the stub is linked with a `0x100000` stack.

The branch is `binfmt.c`, pure and fuzzed: one classifier over one read of the
leading bytes, four verdicts in a written order, the `#!` line parsed to the
kernel's corners, and the kernel's vector rebuild at each of at most four hops,
with the limit doubling as the cycle detector. DR-0027. `dispatch.c` either
takes the ELF case — starting the stub with the window already reserved in it —
or hands the case back with the file and the rebuilt vector, so the host path
and the ELF path cannot disagree about what a chain means. `stub.c` adopts the
window, confirms Control Flow Guard and shadow stacks are off, and places and
enters through WP-31, WP-32 and WP-40.

Both halves of the done-when are measured in `loader/exec/t/`. A static ELF
linked at `0x400000` runs from a Cygwin program through the branch and reports
by leaving, seven bits for seven checks, so 127 is the only pass; a `#!` script
still works, a two-hop chain comes out in the kernel's order, a cycle is refused
by name, and a host image comes back as the host's. Two things were found by the
specimen dying rather than by reasoning: the host's C library cannot be called
while the ELF stack is in force, because Cygwin locates its per-thread state
from the stack pointer, and a hand-written thunk's shadow space has to be
counted for the stack it has rather than copied from a prologue that pushed an
odd number of words.

The exec obligations this package inherits rather than implements — descriptor
inheritance and close-on-exec, the working directory, signal disposition, the
environment — stay with the spawn path that already discharges them for the PE
case. winsup is not in this tree, so the branch is certified through
`elfsysv-exec`, a front end that calls it exactly as that spawn path will.

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

Delivered 2026-08-30, under `loader/fork/`. Cygwin's fork carries everything the
loader keeps in its own writable data, so the object table, the search
configuration, the module table and the rendezvous arrive in the child by the
copy. Three things do not, and the package is the three, with DR-0029 recording
why each is what it is.

Address space the loader took outside the host's bookkeeping is not replayed --
WP-32 maps through the host's mmap so that it is, but WP-41's window is a bare
reservation and nothing records it. So the loader keeps a manifest, the parent
packs it, and the child replays it before anything else runs. `manifest.c` is
parsed as hostile input and fuzzed against a guard page under the
undefined-behaviour sanitizer, with the output array between canaries and every
acceptance re-derived from the bytes: 200000 cases, no crash and nothing written
outside the array.

The loader lock is the one that bites, and the bracket is POSIX's in both
halves. Prepare handlers run in the reverse of registration and then the lock is
taken, so it is the innermost thing held across the call; the parent releases
and runs its handlers in order; the child initializes over the lock rather than
unlocking it, because its only thread is a copy of the thread that took it
rather than that thread. The thread pointer follows: DR-0003's carrier is keyed
to NtTib.StackBase and the child's initial thread has a different one, so
WP-30's `elfsysv_tp_reestablish` is called here and only here.

What crossed is compared rather than asserted. The audit reduces the state to
one record -- every object's bias, dynamic section and mapping, the search
configuration, the static TLS layout, this thread's whole DTV slot by slot, the
`r_debug` structure and its address, and the loader's own code address -- and the
child diffs it and names the first field that moved. A child that does not match
is refused and its handlers do not run.

Both halves of the done-when are measured in `loader/fork/t/`. A second thread
loops through the real `dl_open` and `dl_close` of a cross-linked plugin, holding
the loader lock and sleeping inside it, while the main thread forks; the child
calls `dlsym` and calls into the object across the ABI boundary and reports six
bits, so 63 is the only pass, and a child that deadlocked is killed by its own
watchdog and comes back as a signal rather than a status. The rebase failure mode
is confirmed absent rather than assumed absent by having every child report the
loader's code address and the base of `cygwin1.dll`; over the runs taken neither
moved. `vfork` and `posix_spawn` run the same phases through the same front end.

One defect was found by the certification rather than by reasoning, and it is the
kind only a real fork finds: the forking thread's TCB was a field of the fork
state, so a stage that forked from the main thread after an earlier stage had
forked from a managed thread since joined handed the child a freed TCB to
re-establish from. It is an argument to the prepare call now.

The bracket is applied by the callers rather than by `dl_open` itself, since
WP-38 delivered a single-threaded surface and deferred the per-thread carrier
here; moving it inside the `dl` entry points is identified and not done, and
DR-0029 carries it.


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

Delivered 2026-08-30, in `runtime/signal/`. The frame is Linux's `rt_sigframe`
with the offsets a compiled handler computes for itself asserted rather than
described, an `fxsave` image with an `xsave` area above it carrying the magic
at both ends, and a placement that subtracts the psABI's 128 bytes before it
subtracts anything else. That is DR-0006 executed at the delivery site.

Two things the writing found, and DR-0030 records both. The frame cannot be
built by the thread that sends the signal: a Windows stack grows by faulting on
a guard page, and that fault is a stack extension only for the thread that owns
the stack, so the sender's write into the target takes an unhandled violation
in the sender. The hijack therefore only redirects the target into a trampoline
and the target builds its own frame, which is Cygwin's own shape. And the
return is a same-privilege `iretq`, because `setcontext`'s push-and-ret writes
at the destination stack pointer minus eight, which is the first word of the
red zone this package exists to keep.

The measurement DR-0006 sent here was taken against a control arm rather than
against Cygwin's `sigdelayed`, because an ELF process no longer goes through
`sigdelayed` at all. Over 500 deliveries per arm, a hand-written leaf whose
accumulator lives only in its red zone folded correctly with the reservation on
and broke with it off, and the reservation's cost ranged from -23% to +16% of a
delivery across runs -- below the noise of a path that costs three system calls
and a thread suspension. The control arm is the part that matters: it is what
`spike/cygwin-from-source` lacked when its own measurement silently watched the
wrong bytes.

What is not here: deferring a delivery that lands inside `cygwin1.dll`, the
per-thread split of the blocked mask and alternate stack, and the down-call
wrapper that will consult the `SA_RESTART` decision. DR-0030's Not verified
section carries all three.

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

Deferred: the rest of the C library header surface, and rewiring the toolchain
sysroot to consume `veneer/include/` (a WP-14/WP-15 follow-up).

Redone 2026-08-30 to comply with DR-0000. The re-authored `features.h` was the
drift DR-0000 names: above the floor, el8's files are used unchanged, headers
included. The header set is now el8's own `glibc-headers-2.28`, vendored
verbatim — `features.h`, `stdc-predef.h`, `sys/cdefs.h`, `bits/wordsize.h` and
`bits/long-double.h` are byte-identical to the vendor's, and `gnu/stubs.h` is
the sole justified exception, rewritten to name the interfaces the veneer does
not provide (WP-52's fourth bucket). `veneer/t/ftm-diff.sh` now certifies
byte-identity against the pinned vendor headers rather than diffing re-authored
arithmetic. DR-0010's copy-versus-paraphrase framing is superseded by DR-0000.

### WP-51 — the version map

Needs: WP-20, WP-50.
Delivers: every node from `GLIBC_2.2.5` through `GLIBC_2.28` with every symbol
at the node el8's own glibc put it at, extracted from vendor binaries and
committed as a generated artifact.
Done when: the extraction reruns and reproduces the committed map, and the node
set matches what `readelf -V` prints for the vendor's `libc.so.6`.

Several thousand symbol-to-node bindings maintained by hand would be wrong
within a month, so this is generated or it is not trustworthy.

Delivered 2026-08-30 in `veneer/version-map/`. The map is extracted from nine
vendor binaries by a parser that reads `.dynsym`, `.gnu.version` and
`.gnu.version_d` directly, and its 4024 symbol-to-node bindings and 77 version
nodes are committed as `glibc-version-map.tsv` and `glibc-version-nodes.tsv`.
`libc.so.6` carries the base node plus the 29-node ladder `GLIBC_2.2.5` through
`GLIBC_2.28` and `GLIBC_PRIVATE`, the historical gaps at 2.19–2.21 included.
The nine libraries come from three pinned el8 packages, not one: `glibc` carries
seven, `libnsl` and `libxcrypt` the other two, since el8 built glibc
`--disable-crypt` and `libcrypt.so.1` is libxcrypt with an `XCRYPT_2.0` node the
map carries faithfully — DR-0013. `t/reproduce.sh` reruns the extraction, diffs
it against the committed files, asserts the ladder equals `readelf -V`, checks
`memcpy@@GLIBC_2.14`/`memcpy@GLIBC_2.2.5`, and confirms all 2358 of
`libc.so.6`'s defined dynamic symbols match `readelf --dyn-syms` line for line.
The vendor binaries are fetched and checksum-pinned per DR-0002, not vendored.

### WP-52 — the resolution classification

Needs: WP-51.
Delivers: every symbol in the map sorted into one of four buckets — forwards to
a runtime export under another name, forwards under the same name, needs a shim
because the semantics differ, or has nothing behind it and becomes a stub that
fails predictably.
Done when: the four buckets partition the map with no symbol unclassified; the
fourth bucket is published rather than filed; and no alias is classified less
strictly than its ultimate target — a forward-alias whose target is a shim is a
shim through the same translation, a forward-alias of a stub is a stub — with the
reproduce test asserting zero violations (F2).

That fourth bucket is the honest inventory of what this platform does not have.
It is the most useful document the project will produce for anyone deciding
whether to depend on it.

Delivered 2026-08-30 in `veneer/classification/`. `classify.py` matches the
4024-row version map against the runtime's export surface
(`runtime/exports/cygwin-exports.tsv`) and partitions it: 353 forward under
another name, 1614 under the same name, 192 need a shim, and 1797 have nothing
behind them. The 68 version-node identity objects carry a fifth disposition,
`scaffold`, rather than being mis-filed as failing stubs — DR-0017. The 192
shims are the semantic calls no name match can make; each is flagged for review
and read from a curated `semantic-review.tsv`, grounded in the divergence
classes DR-0000 names (struct layouts, constant values, errno numbers), not
guessed. The fourth bucket is published as `doc/what-the-veneer-lacks.md`, which
separates glibc's own internals (`_IO_`, `__` helpers, `_dl_`) from the public
interfaces a package will actually miss — the Linux-only `epoll`/`inotify`/
`statx`/`memfd_create` family, Sun RPC, GNU argp, `backtrace`, and the `_FloatN`
math surface. `t/reproduce.sh` reruns the classification, asserts the partition
covers the map one-to-one with nothing unclassified, checks every shim is
flagged, and confirms the published document still states the generated counts.

Redo (F2, 2026-08-30): the classification is regenerated under the
alias-strictness invariant in the Done-when above. The known instance is the
acceptance case — `open64` and `__open64`, delivered as bare forwards, become
shims over the same flag translation `open` gets, or carry a written reason they
need none, which the `O_*` table from WP-55 can settle mechanically. The redo
earns a decision record stating the invariant, in the WP-50 redo manner, and
supersedes nothing.

### WP-53 — libc.so.6

Needs: WP-52, WP-36.
Delivers: the ELF `libc.so.6` itself, versioned aliases resolving into
`elfsysv1.dll`, plus the static side (`libc.a` and the startup files, which
WP-14 consumed in draft).
Done when: `memcpy@GLIBC_2.2.5` and `memcpy@@GLIBC_2.14` both live in the object
and bind independently, and spike 4's `elfdeps` result reproduces against the
real library rather than against the synthesized one.

Delivered 2026-08-30, in `veneer/libc/`. `build-libc` turns WP-51's map and
WP-52's classification into `libc.so.6`: 2329 exported symbols over the
29-node `GLIBC_2.x` ladder, each at the node el8's own library assigns it, plus
the 29 version-node identity objects the linker emits from the version script
rather than we from the assembly — DR-0017's distinction, which showed up first
as a duplicate-definition link error. The generated version script names every
symbol under its node instead of declaring the nodes and leaving the assignment
to `.symver`; the shorter script links successfully and silently drops three
quarters of the surface, which is DR-0026. `libc.a` comes out of the same pass
under bare names, since an archive has no version table to hold a compat
binding. The bodies are stubs; `libc-forward.tsv` records the `elfsysv1.dll`
export each entry is to reach. `t/run-tests.sh` builds the library and makes
seven checks against the linked file: both `memcpy` bindings present at two
addresses, the whole export set equal to the map, the emitted node tree equal
to the vendor's node list name and parent, and the rpm provides equal to spike
4's recorded 30 lines — reimplemented from the file format rather than rerun,
which needed a network and an el8 host. A fuzz pass over 2977 mutants checks
the reader refuses rather than crashes.

### WP-54 — the companion libraries

Needs: WP-53.
Delivers: `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`,
`libcrypt.so.1`, `libresolv.so.2`, `libnsl.so.1`, `libutil.so.1`.
Done when: a vendor binary's `DT_NEEDED` list is satisfied entirely from our
tree with no name left over.

el8 still ships these as separate objects rather than the merged glibc of later
releases, so the partition follows el8's rather than a modern one.

### WP-55 — the translation tables

Needs: WP-50, DR-0007.
Delivers: the divergence classes DR-0000 names, as generated tables rather than
as knowledge in someone's head — the errno value map, the signal number map, the
flag constant maps (`O_*`, `F_*`, `AT_*`, `MAP_*`, `SOCK_*` and their relatives),
and layout descriptors for the structs that cross the boundary: `stat`, `dirent`,
`termios`, the `sockaddr` family, `rlimit`, `sigaction`, and whatever else the
extraction finds differing. Each table is extracted mechanically from el8's
vendored headers on one side and the WP-26 tree's on the other, committed with a
reproduce test in the WP-51 manner.
Done when: the extraction reruns byte-identically, every class DR-0000 names has
a table, and every table has a named consumer in the WP-56 shim set or a written
reason it has none.
Risk: a divergence the extraction cannot see — a field with the same name,
offset and size whose meaning differs. The differential in WP-56 is the net
under this package, not the package itself.

### WP-56 — wiring the bodies, in slices

Needs: WP-27, WP-55, WP-52 (redone), spike 12.
Delivers: the forwards become real resolutions into `elfsysv1.dll` and the shims
become translations through WP-55's tables, sliced by subsystem — stdio, memory,
filesystem, process, sockets, time, and so on down the headers — with the slice
order taken from spike 12's demand ranking rather than from anyone's guess.
`libc-forward.tsv` stops being a promise and becomes the generator's input.
Done when, per slice: the slice's symbols pass a differential against a real el8
userland in the WP-T2 environment, over glibc's observable behaviour for that
slice. Done when, overall: a named small vendor package — chosen by spike 12,
built by WP-T4's harness in embryo — compiles, links, runs its own test suite,
and passes it.
Risk: this is the long pole and it always was; the point of cutting it now is
that the plan's tail stops implying otherwise. The per-slice bar keeps it honest
at every step rather than at the end.

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
    WP-26 ─► WP-27 ─► WP-56 ─► (WP-54, WP-62)
    WP-55 ────────────┘

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
