# Roadmap

Everything that has to be written before an el8 userland builds against this
tree, in the order the pieces depend on one another. It is a scope document
rather than a status one. Nothing below records what is finished, because
nothing is, and because scope and progress rot at different rates; a file that
carries both goes stale at the faster of the two.

`milestones.md` holds the five spikes and stops at their verdicts.
`elf-technical-breakdown.md` says why each layer exists and where its material
comes from. This one says what the layer contains, and what condition it has to
satisfy before anything is allowed to lean on it. The reasoning is not repeated
here; the inventory is not repeated there.

## The path assumed

Three points are decisions rather than tasks, `AGENTS.md` reserves them, and a
roadmap that forked three ways under every heading would be unreadable. So this
is written along the recommended path, with the branch named where it sits. One
of the three is now settled and the other two are still assumptions.

| Decision | Taken as | Standing | If it goes the other way |
|---|---|---|---|
| TLS model | ELF-standard `%fs`-relative, base written with `wrfsbase` | Assumed, gated on spike 1 | A TLS model of our own, TEB-slot or emutls. The TLS section changes shape; the sections above it do not. |
| Runtime face | `elfsysv1.dll`, System V outward over an MS-ABI core | Assumed, gated on spike 3 | Unmodified `cygwin1.dll` beneath a generated thunk layer at glibc's export width. The veneer stops being aliases and becomes code. |
| Target triple | `x86_64-elfsysvnt-linux-gnu` | Decided 2026-08-29, DR-0001 | Masquerade as `x86_64-pc-linux-gnu` and move the honest name to `EI_OSABI`, `.note.ABI-tag`, the loader SONAME, and `uname`. DR-0001 carries the share of affected packages at which that is reopened. |

Only the middle row reshapes the program. A negative on spike 1 costs a layer's
worth of design and leaves the dependency graph intact. The third row is no
longer a gate at all: spike 5 still runs, and what it now produces is the size
of the patch set the decision commits to, read against a threshold written down
in advance. A negative on spike 3 moves the convention change from the
runtime's export surface up into the veneer, where it is dearer at every call
and at every version node, and the plan below would need rewriting from that
section outward.

## Order of construction

The breakdown is arranged leaf to trunk, which is the right order for
understanding it and the wrong order for building it. Construction runs
toolchain first, because nothing else can be compiled until something emits ELF
for this target, and it ends at the veneer, because the veneer is what every
other piece was climbing toward.

    toolchain -> runtime -> TLS -> loader -> versioning -> process image
              -> exec -> fork -> signals -> veneer -> debugging -> packaging

Test infrastructure is not a stage in that chain. It runs alongside from the
first line of the loader, and its own section says what it has to cover.

## 1. The toolchain and the target definition

Routine cross-toolchain work, and the gap is labor rather than invention.
Decades of precedent apply. It is first because it is the only thing that can
be built with tools that already exist.

The target definition itself comes before any of it: a triple, an `EI_OSABI`
value, a `.note.ABI-tag` payload, a dynamic linker SONAME, and the string
`uname` reports. The triple is settled, in DR-0001; the other four are not.
Those five have to agree. They also have to be written down
somewhere a later reader can find them, because every one of them ends up
compiled into shipped artifacts (a `.note.ABI-tag` is in every binary, and the
SONAME is in every dynamic object), where changing one means rebuilding the
world.

`config.sub` and `config.guess` recognize the triple. Upstream's `config`
repository takes one small patch; that part is easy. The hard part is that every
package carries its own vendored copy, at whatever vintage its last release
captured, so the real work is a refresh policy rather than a patch.

Binutils targets ELF for `x86_64-elfsysvnt-linux-gnu`: `bfd` target vector,
`ld` emulation, default script, and the ELF backend. Symbol versioning arrives
free here, and that is the point of the whole exercise. `--version-script`
works because the output format is ELF, and `.symver` assembles because `as`
supports it on ELF, so the trap recorded in `symbol-versioning-formats.md`
never opens.

GCC targets the same triple. The specs file carries what the target mandates
rather than what the user remembers: `-mno-red-zone` unconditionally, the
default TLS model, the dynamic linker path, the `.note.ABI-tag` emission. A
package that forgets a flag must still get the flag. The red zone is not a
preference; one object compiled without `-mno-red-zone` will corrupt a stack
under signal delivery, at some unpredictable later date, in a package nobody
was looking at.

Then the sysroot layout, `libgcc`, the startup files (`crt1.o`, `Scrt1.o`,
`crti.o`, `crtn.o`), and the bootstrap order that gets from a
compiler-with-no-libc to a compiler-with-our-libc. `libstdc++` follows once the
veneer exists, along with the rest of the language runtimes any package in the
set needs.

Last, the build macros. These are cheap. Every package rebuilds anyway, so
`-mno-red-zone` lands once in the macro set rather than in four hundred spec
files. Hand-written assembly inside packages that assumes a red zone is the
residual risk, and it wants a ledger rather than a hope.

## 2. The runtime, `elfsysv1.dll`

Cygwin's runtime rebuilt with a System V export surface, bilingual inside. The
digit in the name is load-bearing. It inherits Cygwin's backward-compatibility
rule rather than reinventing it, which means the DLL needs its own API major
and minor counters, plus its own changelog discipline, from the first release
rather than from the first break.

Outward, every export is System V. Inward, every imported Windows function is
wrapped `ms_abi` exactly once, and that wrapping is mechanical enough to
generate from the import list. That direction is the easy one.

The treacherous set is the other direction: the calls Windows makes into the
DLL. `DllMain`, thread entry points, APCs, TLS callbacks, vectored exception
handlers, and the whole fault path, since Cygwin's signal delivery rides
Windows SEH and MS-format unwind data. Each of those needs an MS-ABI entry with
SEH unwind data, and any callback the ELF world hands down to Windows needs a
System V to MS trampoline. Miss one and the convention leaks out the bottom,
quietly, in a way that shows up as corruption rather than as a link error.

The vararg surface needs its own pass. The two conventions disagree about
register save areas and about how a variadic call is set up, so the `printf`
family and everything shaped like it gets handled deliberately rather than by
the same generated wrapper as the fixed-arity calls.

The export list is generated from `cygwin.din` and its aliases, and it becomes
the input to the veneer. Both objects read one list, or they drift.

Everything here compiles `-mno-red-zone`, including the parts that never touch
ELF code, because the boundary between the two halves is not visible to the
compiler. There is no partial application of that flag.

If spike 3 answers no, this section is replaced rather than amended. The
fallback keeps `cygwin1.dll` unmodified and puts a generated thunk layer above
it at the width of glibc's export list: dearer per call, dearer per version
node, and independently testable piece by piece.

## 3. Thread-local storage

Small. It decides the shape of two layers above it, which is a poor ratio of
size to consequence.

A thread's FS base is established at creation and re-established after every
event that could disturb it, which on this host means thread entry, fork in the
child, and whatever spike 1 discovers about context switches. The TCB sits at
that base in the psABI's variant II layout, with the thread pointer at the top
of the static block and negative offsets running down into it.

The static TLS block is sized by the loader at startup from the `PT_TLS`
segments of the initial object set, with surplus reserved for objects that
arrive later through `dlopen`. Getting that surplus wrong is a runtime failure
in a library the program did not know it would load, so the reservation is a
tunable with a documented default rather than a constant.

The dynamic side is `__tls_get_addr`, the dtv, and its lazy per-module
allocation, plus TLS descriptors if the toolchain emits them. Initial-exec and
local-exec resolve to offsets at link time and cost nothing; general-dynamic
goes through the call. All four models have to work; vendor sources choose
among them, and we do not get a vote.

Teardown matters as much as setup, and it is easier to forget. A thread that
exits without releasing its dtv leaks per-module blocks, and a process that
runs long enough, with enough `dlopen` traffic, will notice.

## 4. The dynamic loader

The largest single component, and the one with the best model to work from.
musl's `dynlink.c` is around four thousand readable lines and it is the working
shape for the relocator and the lookup. It is a model rather than an ingredient;
the versioning it leaves out is the next section, and it is the reason this
project exists.

Parsing comes first. It is adversarial from the first byte. The ELF header,
the program headers, `PT_DYNAMIC`, and every table reached through them arrive
as attacker-shaped input, and a truncated or self-referential structure must
produce a diagnostic rather than a fault.

Mapping runs through Cygwin's `mmap`. That is not a detail; it is the mechanism
that makes `fork` work later. Windows reserves address space at 64 KB
granularity against ELF's 4 KB segment alignment, so segments are placed by
reserve-and-commit inside one region per object, and cross-process text sharing
is given up in exchange. If el8 binaries do carry the linker's 2 MB
`max-page-size` default, the arithmetic gets easier; one `readelf` against a
vendor binary settles it, and it has not been run.

Then the object graph. `DT_NEEDED` walked breadth-first, `DT_SONAME` as the
identity, `DT_RPATH` and `DT_RUNPATH` with their precedence difference,
`LD_LIBRARY_PATH`, the system search path, and a cache with an `ldconfig` to
build it. Those five became real loader features the moment the format became
ELF, which retires a stack of workarounds the PE route needed.

Relocation covers the `R_X86_64_*` set that el8 objects actually contain, `RELA`
throughout, `RELR` if the toolchain emits it, and `IRELATIVE` with the ifunc
resolvers behind it. Lazy binding through the PLT and `BIND_NOW` both. Both, not
either: `RELRO` wants the second, and startup time wants the first.

Lookup needs the GNU hash table and the SysV one, correct scope ordering
(global scope, then the object's own dependency list, then anything a `dlopen`
brought in with `RTLD_GLOBAL`), and the interposition rules that make
`LD_PRELOAD` mean what it means on Linux.

Initialization order is `DT_PREINIT_ARRAY`, then dependencies before dependents,
then `DT_INIT` and `DT_INIT_ARRAY`; finalization is the reverse, and a cyclic
dependency graph has a defined tie-break rather than an accident.

The `dl` surface is `dlopen`, `dlsym`, `dlvsym`, `dlclose`, `dlerror`,
`dladdr`, `dladdr1`, `dlinfo`, and `dl_iterate_phdr`. That last one is not
optional decoration; the unwinder finds `.eh_frame` through it, so C++
exception handling depends on it being right.

`LD_DEBUG`, `LD_BIND_NOW`, `LD_PRELOAD`, and `LD_TRACE_LOADED_OBJECTS` round it
out, with `ldd` on top of the last of them.

## 5. Symbol versioning

A few hundred lines on top of the relocator. It is the reason for the entire
edifice, and it is the smallest thing in this document.

The matcher reads `.gnu.version`, `.gnu.version_d`, and `.gnu.version_r`,
resolves each undefined symbol against the version its requirement names, and
refuses to load when a required node is absent unless the requirement is marked
weak. `VER_NDX_LOCAL` and `VER_NDX_GLOBAL` have their special meanings, the
default definition marked `@@` is what an unversioned reference binds to, and a
non-default `@` definition binds only to a reference that asks for it by name.
Version definitions form a chain with parents, and the chain is what lets
`GLIBC_2.28` imply everything below it.

Drepper specified this to the bit, twice, and glibc's resolver is the only
complete implementation. It is GPL and it assumes a Linux kernel underneath, so
it informs the design and does not join it. Writing from the specification is
the plan of record.

Downstream, this is what rpm's `elfdeps` reads. Spike 4 measures whether it
does before anything large is funded.

## 6. The initial process image

Small, exact, and solved elsewhere. The kernel's job, done by us. The back half
of *Userland Exec*, in other words, with our content poured into their mold.

The stack is built downward: `argc`, the `argv` pointers, a null, the `envp`
pointers, another null, then the auxiliary vector, then the strings and the
`AT_RANDOM` bytes they point at. The entry point is reached with `%rsp`
pointing at `argc` and the ABI's alignment honored, with `%rdx` carrying the
`atexit` handler or zero.

The auxv has to describe our world honestly enough for a dynamic linker to
initialize against it. `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_ENTRY`, `AT_BASE`,
`AT_PAGESZ`, `AT_FLAGS`, `AT_UID`, `AT_EUID`, `AT_GID`, `AT_EGID`, `AT_SECURE`,
`AT_CLKTCK`, `AT_RANDOM`, `AT_PLATFORM`, `AT_HWCAP` and `AT_HWCAP2`, and
`AT_EXECFN` all have answers. `AT_SYSINFO_EHDR` does not, because there is no
vDSO here, and every function that would have gone through one goes through the
runtime instead. A consumer that treats the entry's absence as fatal is a bug
to find early.

`AT_PAGESZ` is the one to think about twice. Windows commits at 4 KB and
reserves at 64 KB (the two numbers disagree, and only one of them can be
reported), while code downstream does arithmetic with whatever we say.

## 7. exec dispatch and the PE host stub

A magic-byte branch inside Cygwin's spawn path, which already forks a stub and
already recognizes `#!`. Linux keeps the same dispatch in the kernel and calls
it `binfmt_misc`. Moving the table into libc changes where it lives. Nothing
else moves.

The branch reads the first bytes, recognizes ELF, and routes to our path
instead of to `CreateProcess` on the file. Ordering against the `#!` case and
against the existing PE case has to be defined rather than inherited by
accident, and the interpreter recursion limit is part of that definition.

The stub itself is a PE image with almost nothing in it: reserve a large region
for the ELF world, opt out of CET shadow stacks and Control Flow Guard, load
the runtime, hand the loader the file and the argument vector, and get out of
the way. It carries the ELF world's address space and none of its semantics.

Around that sit the ordinary `exec` obligations. Descriptor inheritance and
close-on-exec, the environment, the working directory, signal disposition
reset, and `AT_SECURE` for the cases where the image is not trusted to inherit
everything. Cygwin's `execve` never could replace a process image, so the
parent-stub fiction is inherited rather than introduced.

## 8. fork

Cygwin's `fork` already replays what went through Cygwin's `mmap`, which is why
the loader was made to allocate that way. So the work is an audit. Make sure
nothing escaped.

Loader bookkeeping crosses into the child intact: the object list, the search
paths, the TLS block and every dtv, the `r_debug` structure and its address.
Threads do not cross, per POSIX, so the child re-establishes a single thread's
FS base and reconciles anything the loader recorded per-thread.

`pthread_atfork` handlers run in their specified order, and the loader's own
lock is one of the things that has to be held across the call and released on
both sides, or a `fork` from a thread that was inside `dlopen` deadlocks the
child.

Because ELF position-independent objects relocate anywhere, the DLL rebase
hazard that haunts Cygwin's `fork` is removed rather than added to. That is a
gain, and it is worth confirming rather than assuming.

`posix_spawn` and `vfork` take the same path with the same care.

## 9. Signals

Cygwin delivers a signal by hijacking the target thread. The trampoline that
lands that delivery on an ELF-side stack is ours to write. That is workable
only because both sides are ours; a foreign binary would make it impossible.

The frame the handler sees has to match what an ELF-world consumer expects:
`siginfo_t` laid out as the psABI and the Linux headers agree, `ucontext_t` and
`mcontext_t` with the register slots in their specified order, the FPU and
extended state saved where a consumer looks for it, and a return path that
restores all of it. `sigaltstack` works, `SA_SIGINFO` works, `SA_RESTART` means
what it means.

The red zone is the sharp edge. It is a specification guarantee the host does
not honor: Windows exception and APC dispatch writes below `%rsp`, and System V
says nothing may. Compiling the world `-mno-red-zone` closes it for compiled
code. Hand-written assembly is where it stays open.

Unwinding does not cross the boundary raw. DWARF and `.eh_frame` stay in the
ELF world, SEH stays in the host-facing core, and the trampoline is the only
place they meet. That is Wine's rule, restated, and it is a rule rather than a
guideline because the failure mode is an unwinder walking into frames it cannot
read.

## 10. The libc veneer

The trunk. Nothing like it exists. A real ELF `libc.so.6` exporting
`GLIBC_2.x`-shaped version nodes, whose bodies resolve into `elfsysv1.dll`, so
that `memcpy@GLIBC_2.2.5` and `memcpy@@GLIBC_2.14` both live in one object and
rpm reads the vendor's exact dependency shape off it.

The version map is derived from el8's own glibc rather than invented: every
node from `GLIBC_2.2.5` through `GLIBC_2.28`, every symbol at the node the
vendor put it at. That map is generated from the vendor's binaries and
committed. A hand-maintained copy of several thousand symbol-to-node bindings
would be wrong within a month.

Each symbol then resolves one of four ways, and the classification is a
deliverable in itself. It forwards to a runtime export under a different name;
it forwards under the same name; it needs a small shim because the semantics
differ; or the runtime has nothing and the symbol is a stub that fails
predictably. That fourth bucket is the honest inventory of what this platform
does not have, and it wants publishing rather than hiding.

The companion libraries come with it, because el8 binaries link them by name:
`libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libcrypt.so.1`,
`libresolv.so.2`, `libnsl.so.1`, and `libutil.so.1`. On el8 these are still
separate objects rather than the merged glibc of later releases, so the
partition follows el8's.

The static side is the startup files and `libc.a`, and the headers are the
other half of the veneer: a glibc-shaped `features.h`, `__GLIBC__` and
`__GLIBC_MINOR__` reporting el8's numbers, and the feature-test macro behavior
that goes with them. A package that probes the headers and then links the
library must get one answer, not two.

One closure stands, and it is not repairable at any price. glibc itself needs a
Linux kernel personality underneath, so the C library remains newlib plus
Cygwin wearing a glibc-shaped, versioned ABI surface. The veneer is a face, not
an implementation.

## 11. Debugging

The conceded cost. Bridged where a protocol already exists, and conceded where
none does.

The SVr4 `r_debug` rendezvous is how a dynamic loader announces its objects to
a debugger: a structure at a known address, a link map the loader keeps
current, and a function the debugger breakpoints so it learns when the map
changed. Wiring it costs little and it should be wired the day the loader can
load a second object, because the alternative is debugging a world no tool can
see.

A gdb built for the triple consumes that and sees everything. Building it is
toolchain work; it is named here because this is the reason it matters.

Unwinding through the ELF world needs `PT_GNU_EH_FRAME` and a working
`dl_iterate_phdr`, both of which the loader owes anyway.

Core dumps are an open question rather than a plan. An ELF core that our gdb
reads is the useful artifact; what Windows produces on a fault is a minidump of
a stub. Deciding between writing the first and tolerating the second can wait,
but not indefinitely.

What stays broken is the Windows-side view. A debugger or a dependency walker
attached from outside sees a PE stub and anonymous executable regions. That is
Wine's situation inverted, and Wine's remedy transfers: own the debug channel.

## 12. Packaging and the rpm surface

Most of this repairs itself the moment the format is ELF. The list is worth
stating anyway, because the PE route needed a workaround for every line of it.

`file` reports ELF, rpm's magic gate matches, `elfdeps` fires unmodified, and
`Provides` and `Requires` take the vendor's exact
`libc.so.6(GLIBC_2.2.5)(64bit)` shape. No dependency generator has to be
written. The stage 0.5 admission recorded in `symbol-versioning-formats.md` for
a PE generator does not apply here, and it should be marked superseded rather
than left to confuse a later reader.

What is left is ours: `ldconfig` and the cache format, the search path
configuration it reads, and the macro set that puts `-mno-red-zone` and the
triple into every package build.

Distribution carries two constraints that are not defects. Self-mapped
anonymous executable memory is malware-shaped, so enterprise endpoint
protection will object to the loader permanently, and exclusions are a
deployment step rather than a bug to fix. And every installer or configurator
here is idempotent, reseeding derived configuration from a pristine template
each run rather than editing it in place forever.

## 13. Test infrastructure

Not a stage. It starts with the loader's first line and runs beside everything.

The loader, the relocator, and the version matcher parse hostile input from the
moment they run, so they get unit tests over recorded fixtures and a fuzz
target fed malformed and truncated ELF. A relocator that has never seen a
truncated `PT_DYNAMIC` is not finished; that is a rule, not a sentiment. The
fixture corpus is committed, small, and includes the ugly cases: zero-length
segments, overlapping `PT_LOAD`, a `DT_NEEDED` pointing past the end of the
string table, a verneed chain that loops.

Differential tests against Linux cover everything with a specified answer and a
real implementation to compare against. TLS layout, auxv contents, the
`r_debug` structure, symbol resolution order, and the version matcher's verdict
on a library that a real `ld.so` also has an opinion about. Checking against a
running glibc beats checking against our reading of the specification.

Spikes get transcripts. A spike whose script no longer regenerates its recorded
result is a failing test rather than an old file, and it gets fixed on that
footing.

Above all that sits the thing the program is actually for: taking a vendor
source package, building it against this tree, and comparing what comes out
against what Red Hat shipped. That comparison is `rhelcyg-8.10`'s concern, and
it is named here because it is the acceptance criterion the rest of this
roadmap serves.

## Not verified

Recorded so a later reader does not mistake these for measured.

The two assumed answers still in the table at the top. Spikes 1 and 3 exist to
replace them, and neither has run. The third row is decided rather than
assumed, and spike 5 now prices it rather than settling it.

That el8 binaries carry 2 MB `PT_LOAD` alignment. Recalled binutils default;
one `readelf` against a vendor binary settles it, and the mapping arithmetic in
section 4 rests on it.

That el8's rpm carries `elfdeps` and `fileattrs`. The Cygwin port of rpm 4.18
omits both, and whether that omission belongs to the port or to the version is
untested. Section 12 assumes the former.

That Cygwin's `fork` replays every mapping made through its own `mmap` without
exception. Asserted from the design of both, not measured, and section 8 leans
on it heavily.

The glibc symbol-to-version map as a generated artifact. Nobody has run the
extraction against el8's binaries, so the size of that map is an estimate.
