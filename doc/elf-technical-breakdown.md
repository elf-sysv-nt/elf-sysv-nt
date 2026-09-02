# ELF on a Cygwin runtime: a leaf-to-trunk breakdown

Surveyed 2026-08-20, written in `rhelcyg-8.10` and moved here the same day when
this became its own project. The founding design document: not a plan of record,
and every layer below is a proposal. The question behind it: given that
Windows executes only PE, and PE cannot carry ELF's symbol versioning, could a
user-space loader inside libc `exec*()` sidestep both limits by never handing
the file to the Windows loader at all? This is the construction drawing for that
route: the layers ELF execution actually requires, ordered from the leaves
inward to the trunk, with each layer marked by how much of its foundation is
already poured.

The organizing fact is a gradient, not a list. At the leaves, the peripheral
mechanisms every ELF loader needs, the ground is quarried and the code is
public; some of it is even liftable. Climb toward the trunk, the single
integrated personality that binds those mechanisms to *this* runtime, and the
reusable art thins, then runs out. That is not bad luck. Every open
implementation was written for a different runtime, and the runtime is the one
thing this project changes, so the closer a layer sits to our particular seam
against `cygwin1.dll`, the less anyone has built it before. The foundation is
strong exactly where the work is generic and absent exactly where it is ours.

Three provenance marks appear throughout:

- **Foundation poured** — open, readable, often reusable if the license allows.
- **Framed in papers** — solved and documented in the literature, with no free
  or liftable code behind it.
- **Our ground** — no prior implementation exists for this runtime.

A caution that governs the first mark. "Open" means the source can be read, not
that it can ship in this tree. ISC and MIT code can be reused; GPL and LGPL code
can be studied but not linked into a released image without carrying the license
along. Licenses below are marked recalled until checked, and the check precedes
any lift.

## The gap is not the hardware

Start from what already works. A Linux ELF binary runs on this exact processor,
and the processor does not know or care which operating system wrote its page
tables. Same registers, same instruction encodings, same calling convention;
the silicon executes the binary's instructions identically whether Linux or NT
set the process up. So the instruction stream needs no translation at all, which
is the whole reason design (b) runs at native speed and design (a)'s binary
translator is a cost we can decline.

The gap opens at one precise line: where the binary stops computing and asks the
operating system for something. A mapping. A thread. A signal. A file
descriptor. A symbol resolved to an address at load time. Everything Linux hands
the binary through the kernel and through `ld.so`, this project must hand it
instead, from user space, on top of Cygwin. Bridging the gap is nothing more and
nothing less than supplying every one of those services without a Linux kernel
underneath. The layers that follow are that bridge, built leaf to trunk, and
each one names the gap it closes and the material it closes it with.

## Bedrock — the processor and the ABI

**Foundation poured, and shared with Linux outright.** This layer is not merely
open; it is common ground. The contract is the System V AMD64 psABI, the same
specification Linux compiles against, so at the register and calling-convention
level our target and a Linux binary already agree. Nothing here is built,
because the hardware speaks it natively.

One hairline crack runs through the shared bedrock, and it matters later. The
psABI reserves the 128 bytes below `%rsp`, the red zone, and states that they
"shall not be modified by signal or interrupt handlers." Linux honors that, and
this design does not, so the whole ELF world must compile `-mno-red-zone`.
Spike 3 measured where the guarantee actually goes and it is not where this
paragraph used to say: Windows' own exception dispatch starts writing about 320
bytes below `%rsp` and preemption and thread hijacking write nothing at all, so
the host leaves the reserved 128 alone. Cygwin's signal delivery takes `%rsp-8`
first, because it hijacks the thread and builds the handler's frame at the
interrupted stack pointer. The crack is in our layer rather than the host's,
which makes the flag one repair and a 128-byte gap in the delivery path
another.

FSGSBASE, the instruction family that makes thread-local storage cheap, has
been present since Ivy Bridge, so the silicon for the hardest layer above is
already in every machine this runs on.

## Mapping the image

**Foundation poured.** The gap: Linux's kernel reads the ELF header, maps each
PT_LOAD segment at its address with its protections, and maps the interpreter
named in PT_INTERP. Without a Linux kernel, the loader does this itself, through
Cygwin's `mmap`. The material is abundant and old. The grugq's *Userland Exec*
laid out the sequence in 2004, `ulexecve` and Blink and Cosmopolitan's APE
loader are three living implementations of it, and LBW did it on Windows
specifically. Mapping is a few thousand lines of well-trodden code, liftable in
spirit even where license forbids lifting it in fact.

Poured here too, as of 2026-08-29. Spike 2 reserved the span, committed and
protected one region per PT_LOAD, built the stack, and jumped; the image ran at
its link address and returned. What it added to the recalled account is one
constraint that none of the prior art has to face, because none of it runs
under a runtime that started first: the span has to be claimed before anything
else in the process allocates without a base, since Windows hands out the
lowest free region. `spike/map-and-jump/` carries the measurement.

## The initial process image

**Foundation poured.** The gap: the kernel builds the initial stack, the
argument and environment vectors, the auxiliary vector the dynamic linker
reads, then jumps to the entry point. We build the same stack and jump the same
way. This is the back half of *Userland Exec* and it is small, exact, and
solved. The one subtlety is that the auxv must describe our world honestly
enough that `ld.so` initializes, which is our content poured into their mold.

## The dynamic loader

**Foundation poured, with an extension owed.** The gap: Linux ships `ld.so`,
which relocates the object, fills the GOT and PLT, resolves symbols across
dependencies. We ship ours. musl's `dynlink.c`, near four thousand readable
lines, is the working model for the relocator and the lookup. The extension is
the reason it is not simply reused: musl's symbol-versioning support is
deliberately partial, and the next layer is precisely the part it leaves out.

## Symbol versioning

**Framed in papers.** This is the feature the entire project exists to recover,
and it sits right at the boundary between poured foundation and open ground. The
format is specified to the bit, twice, by Drepper: the ELF symbol-versioning
note and the longer *How To Write Shared Libraries* give `.gnu.version_d`,
`.gnu.version_r`, the version index section, the `@` and `@@` default marking.
glibc's resolver is the only complete implementation. It is LGPL-2.1-or-later,
which this tree could take — LGPLv3 is where an "or later" lands, and the
headers under `veneer/include/` are already vendored from glibc on exactly
that footing. The obstacle is not the licence but the coupling: `dl-lookup.c`
reaches into `_rtld_global`, the `link_map` glibc's own loader builds, its
`tunables`, its TLS layout and its assumptions about being the process's first
mover, none of which exist here, and pulling the file means pulling that
world. So it informs the design without joining it, and the gap closes by
writing a verdef and verneed matcher from the specification, a few hundred
lines on top of the musl-shaped relocator. That matcher is what lets rpm's
`elfdeps` read a vendor-shaped dependency off our libraries.

## Thread-local storage

**Framed in papers, and the one measurement we owed came back no.** The gap:
Linux sets a thread's FS base through `arch_prctl` and the kernel preserves it
across every context switch, which is what makes `%fs`-relative TLS work. The
models are documented and the hardware is present, so the theory was never the
problem. The narrow, load-bearing fact was whether Windows preserves a
*user*-written FS base, and spike 1 measured it on 2026-08-29.

It does not. The instruction is available and the base addresses correctly, so
this is not a hardware or a permissions answer; the base comes back zero after
anything that takes the thread off a processor. Preemption alone does it, in
tens of milliseconds, with no system call anywhere in the case, which closes
the repair of re-establishing the base at a call site. So the bridge is a TLS
model of our own, TEB-slot based or emutls, which is our ground and cheap to
hold because we own the target's definition. Which one it is belongs to the
operator under `AGENTS.md` and is not settled here.
`spike/fs-base-persistence/` carries the probe and the transcript.

## The kernel-ABI seam — the fork in the road

**The widest part of the gap, and where the two designs part.** Above TLS, an
ELF binary's demands on the OS stop being incidental and become the whole Linux
system-call interface. There are two bridges across it, and the choice was
already made in `rhelcyg-8.10`'s `plan-rpm-userland.md`, which rejected the
syscall route once before this project existed; this layer records why.

The first bridge, call it design (a), translates. Catch or rewrite every
`syscall` site and service
it in user space, which is what flinux, QEMU's linux-user mode, and Qiling do.
All three are readable, all three copyleft, so they are study material, not
ingredients. This bridge has been built to completion once, by WSL1, whose
`lxcore.sys` implements the Linux interface inside an NT kernel driver reached
through the pico process seam; it is **framed in papers and sealed**, excluded
by name from Microsoft's 2025 WSL open-sourcing, standing on a provider API
(`PsRegisterPicoProvider`) that the WDK does not document and that admits only
Microsoft-signed drivers. Its parent is Drawbridge, the *Rethinking the Library
OS* prototype that ran a Windows personality in a process over a thin host seam,
paper open and prototype closed. Interix is the cautionary cousin: a real POSIX
personality on NT, built as a PE-based environment subsystem, closed throughout
and removed in 2013. The translation bridge, in short, is flinux's grave for
anything glibc-sized and Microsoft's private property everywhere it was finished.

The second bridge, design (b), refuses to translate. Rebuild the packages
against our runtime so the ELF world calls DLL entry points, exactly as Cygwin
programs already do, and there is no `syscall` instruction left to catch. This is
**our ground**, and it is the design this sketch recommends. The gap here is not
crossed; it is dissolved, by making the far side speak our language at build
time.

## The ABI boundary — elfsysv1.dll

**Technique in the open, placement on our ground.** The gap that opens the
moment the second bridge is chosen: ELF code is System V, Windows code is
Microsoft x64, and the two calling conventions disagree about which registers
carry arguments and who preserves what. Somewhere, every call must change
convention. The design question is not whether but where, and the answer follows
the project's own rule: change the lowest feasible layer when that leaves the
least change above it.

So the runtime library is rebuilt as `elfsysv1.dll`, speaking System V on its
export surface. The name omits the `nt` that the target triple's vendor field
carries, and the omission is deliberate: a DLL is an NT artifact by
construction, so naming it for its host would restate what the extension
already says. What wants stating is the face it presents, which is ELF and
System V. The parallel with `cygwin1.dll` runs down to the digit, because the
backward-compatibility rule the digit encodes is inherited rather than
reinvented. The ELF world above it is then uniformly SysV, top to bottom;
the libc veneer at the trunk collapses from a wall of generated thunks into
plain versioned aliases; no layer above this one knows a boundary exists. The
boundary itself does not vanish, because the DLL has two faces and only one is
ours to choose. Outward it speaks SysV by our decision. Inward it still calls
`ntdll` and `kernel32`, which are Microsoft x64 by OS mandate, so the convention
change moves from the export surface down to the DLL's own descent into
Windows, which is exactly the chokepoint Cygwin already funnels every host call
through. The relocation is the win: the crossing lands where a maintained seam
already sits, instead of being smeared across the veneer.

Confinement is a discipline, not a property. The down-calls are mechanical,
each imported Windows function wrapped `ms_abi` once. The treacherous set is
the calls Windows makes *into* the DLL: `DllMain`, thread starts, APCs, and
above all the fault machinery, since Cygwin's signal delivery rides Windows SEH
and MS-format unwind data. `elfsysv1.dll` is therefore deliberately bilingual
inside, SysV outward with an MS-ABI, SEH-unwound core for everything the host
touches; any callback the ELF world hands down to Windows gets a SysV-to-MS
trampoline, or the convention leaks out the bottom. Wine crosses this same
divide in production with functions gcc compiles under `ms_abi` and `sysv_abi`
attributes, so the mechanics are proven; the placement beneath a whole libc is
ours. Spike 3 crossed it in both directions on 2026-08-29 with every
callee-saved register intact, including the `%rsi`, `%rdi` and `%xmm6` through
`%xmm15` a Windows caller expects back and a System V callee is free to
destroy, and had Windows enter SysV code through a thread start, an APC, a
vectored handler and a signal handler. At one function's width, not a DLL's.

The bedrock crack surfaces here as policy: the DLL compiles `-mno-red-zone`
throughout, which spike 3 confirmed is a flag that changes code generation on
this toolchain rather than a restatement of the default, and the printf family
needs the vararg care the two conventions disagree on -- which turns out to be
more than care. `va_list` here is Microsoft's eight-byte pointer and System V's
is a twenty-four-byte descriptor reached through `__builtin_sysv_va_list`, so
nothing forwards: every variadic export unpacks its arguments and passes them
on by value. The fallback, if the bilingual core proves worse than expected,
is the veneer-thunk design this section previously described:
unmodified `cygwin1.dll` beneath a generated thunk layer at the width of
glibc's export list. Dearer at every call and at every version node, but each
piece independently testable.

## The libc veneer — the trunk

**Our ground, and the reason the edifice is worth building.** Here every layer
below converges. A real ELF `libc.so.6` that exports `GLIBC_2.x`-shaped version
nodes, whose symbol bodies resolve into `elfsysv1.dll`, so that
`memcpy@GLIBC_2.2.5` and `memcpy@@GLIBC_2.14` both live in one object and rpm
reads the vendor's exact dependency shape off it. With the runtime already
SysV-faced the veneer carries versioned aliases rather than convention
thunks, which is what the layer below bought. Nothing like it exists.
midipix is the nearest attempt and it is instructive by contrast: it put musl on
NT, but it chose PE as its object format and had to write its own syscall layer
underneath, which is the opposite of both choices here. We keep ELF, so
versioning is expressible; we already have the syscall layer, as the DLL, so the
veneer is the only new thing. This object is the trunk the whole structure was
climbing toward, and no one has carved it.

## exec dispatch, fork, and signals

**Our ground, built on Cygwin's own engines.** Three integration jobs sit around
the trunk. The exec dispatch is a magic-byte branch inside Cygwin's spawn path,
which already forks a stub, already recognizes `#!`; the ELF branch is a small
Cygwin-specific extension. fork replays the loader's mappings into the child by
routing every one of them through Cygwin's `mmap`, so Cygwin's existing fork
carries them, and because ELF position-independent objects relocate anywhere,
this removes the old DLL rebase hazard rather than adding to it; *A fork() in the
road* is worth reading first as counsel on how much `fork` fidelity is even worth
chasing. Signals bridge Cygwin's thread-hijack delivery onto an ELF-side stack
through a trampoline we write, workable only because both sides are ours. DWARF
unwinding stays in the ELF world and SEH in the host-facing core, never crossing
the boundary raw, which is Wine's rule restated.

## The toolchain and the triple

**Foundation poured, assembly required, the naming decision taken.** A binutils
and gcc that emit ELF for this runtime is routine cross-toolchain work with
decades of precedent; the gap is labor, not invention. The decision that stood
open here was the target triple, and it was settled on 2026-08-29 as
`x86_64-elfsysvnt-linux-gnu`; DR-0001 records it. The argument it settled:
masquerade as `x86_64-*-linux-gnu` and configure probes report maximum Linux
fidelity, then discover epoll or inotify missing at link time; choose an honest
custom triple and the divergence shows sooner and configure results drift from
the vendor's. Probes link and run under either name, so the truth arrives
regardless. The choice was where to put it.

It is narrower than it first looks, because the triple has four fields and only
two carry weight. The four are `cpu-vendor-kernel-os`, which is `config.sub`'s
own naming: it sets `kernel=linux` and `os=gnu`, and validates the pair under
`case $kernel-$os-$obj`. Nothing in it is called `abi`, and the field this
document once called that is the C library, which is why `linux-musl` and
`kfreebsd-gnu` are the two directions the same substitution runs. `config.sub`
passes an unrecognized vendor through untouched, while kernel and libc are what
the machinery reads, so `x86_64-elfsysvnt-linux-gnu` puts the honest name where
it costs least and leaves `linux-gnu` standing where configure looks. Buildroot
has shipped `x86_64-buildroot-linux-gnu` on that reasoning for over a decade,
at the same twenty-six characters, so neither the shape nor the length is new
ground.

Neither of the two load-bearing fields is a lie, and DR-0005 settles the
wording because the older one invited a reopen that would cost the toolchain
its whole dividend. `gnu` is glibc, exactly and without subtraction. `linux` is
the Linux kernel ABI, which this project satisfies along every axis but one:
the second bridge above leaves no `syscall` instruction to catch, so an object
reaching the kernel through a raw `syscall` rather than through a call into
`elfsysv1.dll` sits outside the contract. `doc/target-definition.md` states
that bound and is the place to cite for it.

The residual cost is the package that matches a literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` and misses silently, taking a configure branch nobody
intended. Spike 5 counted those on 2026-08-29, over all 2893 source names in
the el8 set: one package, `flac`, whose `configure.ac` opens `case "$host"`
with a `*-pc-linux-gnu)` arm that defines `FLAC__SYS_LINUX`. Under our triple
that arm does not match and the define silently does not happen. At the
`config.sub` gate the cost is not small but zero — across 1193 vendored
`config.sub` files the masquerade and the honest triple get identical verdicts,
file for file. `spike/triple-fidelity/results-2026-08-29.txt` is the
transcript and that spike's README reads the other eighteen literal matches,
all of which are comments, documentation and test fixtures.

The kernel field is closed off for a reason worth recording, because it is not
the one that would be guessed. `config.sub` does not refuse `elfsysvnt` there;
it accepts it, by matching `elf*`, the entry in the recognized-os list that
exists for bare-metal targets of the `i386-elf` kind. `config.gcc` then reads
`x86_64-*-elf*` as bare metal and routes the triple to a target definition with
no operating system beneath it. A refusal would stop a build; this succeeds and
is wrong, which is the worse of the two. Measured against a 2021 `config.sub`
on 2026-08-20, and still true of upstream's 2026-05-17 file when DR-0005
rechecked it: `x86_64-pc-elfsysvnt` comes back accepted.
`spike/triple-fidelity/` holds the older transcript.

The libc field is closed off harder, and by the script rather than by an
argument. `config.sub` allowlists ten libc values against the `linux` kernel
and refuses the rest outright, so `x86_64-elfsysvnt-linux-elfsysvnt` exits 1
with `Kernel 'linux' not known to work with OS ''`. A refusal is the outcome
worth having. Substituting there would also assert something false in the
opposite direction, since this project's libc is glibc and nothing else.

## Debugging the opaque image

**Protocol in the open, our triple new, opacity conceded.** The gap is the
stated cost of the whole approach: a Windows debugger or dependency walker sees a
PE stub and anonymous executable regions, not the ELF world inside. The bridge is
standard where it counts. The SVr4 `r_debug` rendezvous is the documented
protocol by which a dynamic loader announces its objects to a debugger, and a gdb
built for our triple consumes it and sees everything. Wine walked this exact road
and grew its own debugging channel for the same reason. Wiring a known protocol
to a triple that does not exist yet is the new part; the protocol itself is
public.

## The edifice at a glance

Leaf to trunk, with where each layer's material comes from.

| Layer | Provenance | Leans on |
|---|---|---|
| Processor and ABI | shared with Linux | psABI, Intel FSGSBASE |
| Image mapping | foundation poured | Userland Exec, LBW, Blink |
| Initial process image | foundation poured | Userland Exec |
| Dynamic loader | poured, extension owed | musl `dynlink.c` |
| Symbol versioning | framed in papers | Drepper; glibc LGPL-2.1+, kernel-coupled |
| Thread-local storage | papers; spike 1 refuted `%fs` | psABI, arch_prctl model |
| Kernel-ABI seam | fork in the road | flinux/QEMU (GPL); WSL1, Drawbridge (sealed) |
| ABI boundary, elfsysv1.dll | technique open, placement ours | Wine `ms_abi`/`sysv_abi` |
| libc veneer | our ground, the trunk | midipix by contrast |
| exec, fork, signals | our ground | Cygwin engines; A fork() in the road |
| Toolchain and triple | poured, assembly | standard cross-toolchain |
| Debugging | protocol open | SVr4 `r_debug`, Wine |

Read the table top to bottom and the gradient is plain. The first four rows are
shared silicon or public code. The middle rows are solved on paper with the code
either license-locked or sealed. The bottom rows are ours, and they cluster
around the trunk, the veneer and the exec seam, because that is where the design
meets this runtime and no earlier project stood.

## Spikes, in dependency order

1. FS base persistence. Write the FS base with `wrfsbase`, provoke every event
   that can take the thread off a processor, and read it back. An afternoon,
   and it decided the TLS layer. Run 2026-08-29: the base does not survive,
   and preemption alone is enough to clear it.
2. Map and jump. A PE stub maps a static ELF built for the custom target and
   jumps to it across a hand-built stack and auxv. Proves the two foundation
   layers end to end with no dynamic linking in play.
3. The crossing. Build one runtime entry point SysV-faced over an MS-ABI
   core, call it from an ELF object, deliver a signal mid-call, and inspect
   the 128 bytes below `%rsp`. Confirms the bilingual boundary and the
   red-zone policy by measurement, at one function's width before it is
   attempted at the DLL's. Run 2026-08-29: the boundary holds in both
   directions, and the red zone is lost to Cygwin's own signal delivery
   rather than to Windows.
4. The payoff. Synthesize a versioned `libc.so.6` carrying one verdef node, run
   el8's `elfdeps` against a consumer linked to it, and confirm the
   vendor-shaped `Requires` line appears. Proves the trunk repairs fidelity
   before anything large is funded.
5. Triple fidelity. Feed each el8 package's own `config.sub` the masquerade,
   the vendor-honest candidate and an os-honest control, then grep the set for
   literal vendor matches. Out of dependency order because it is cheap and
   changes no runtime behavior; the deliverable is a count, not a yes or no.

## Sources

Read 2026-08-20 unless noted.

Loader, exec, and emulation, open source

    https://grugq.github.io/docs/ul_exec.txt
    https://github.com/anvilsecure/ulexecve
    https://cowlark.com/lbw/index.html
    https://github.com/wishstudio/flinux
    https://github.com/trungnt2910/HelloElf
    https://github.com/jart/blink
    https://github.com/qilingframework/qiling
    https://www.qemu.org/docs/master/user/main.html

Foreign-loader and library-OS precedent

    https://www.winehq.org/
    https://www.midipix.org/
    https://github.com/midipix-project/mmglue
    https://www.usenix.org/sites/default/files/conference/protected-files/atc17_slides_tsai.pdf

White papers, solved on paper

    https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/asplos2011-drawbridge.pdf
    https://www.microsoft.com/en-us/research/uploads/prod/2019/04/fork-hotos19.pdf
    https://www.usenix.org/conference/2005-usenix-annual-technical-conference/qemu-fast-and-portable-dynamic-translator

Specifications

    https://www.akkadia.org/drepper/symbol-versioning
    https://www.akkadia.org/drepper/dsohowto.pdf
    https://www.uclibc.org/docs/psABI-x86_64.pdf

Toolchain tuples and the vendor field

    https://buildroot.org/downloads/manual/manual.html
    http://lists.busybox.net/pipermail/buildroot/2014-February/090227.html
    https://wiki.osdev.org/Target_Triplet

Sealed but documented

    https://learn.microsoft.com/en-us/archive/blogs/wsl/pico-process-overview
    https://blogs.windows.com/windowsdeveloper/2025/05/19/the-windows-subsystem-for-linux-is-now-open-source/

In-tree: `elf-userspace-execution.md` for the survey and the pricing of the two
designs, `symbol-versioning-formats.md` for the record this work fences,
`milestones.md` for the spikes below in their scheduled form, `ROADMAP.md` and
`IMPLEMENTATION-PLAN.md` for what the layers above become once the spikes have
answered.

In `rhelcyg-8.10`, the first consumer: `plan-rpm-userland.md`,
`stage-model.md`.

## Not verified

Recorded so a later reader does not mistake these for measured.

Which TLS model replaces `%fs`. Settled. Spike 1 answered the recorded question
no on 2026-08-29 (`spike/fs-base-persistence/`, one Windows build and one
processor), the gs-thread-pointer spike measured the replacements the same day,
and the operator took DR-0003: a runtime-owned thread pointer through `%gs`,
carrier C3, the `_my_tls` shape. What is not yet verified is narrower — that
Cygwin's real `_my_tls`, its padding, and its behaviour under an alternate
signal stack match the stand-in the spike measured; WP-30 re-measures that as it
builds against C3.

That the runtime core can be rebuilt SysV-faced without breaking its SEH-based
fault handling, thread entry, and Windows callbacks. Narrowed rather than
settled on 2026-08-29. Spike 3 measured one function's width of it and every
case passed: arguments and callee-saved registers held in both directions,
Windows entered SysV code through a thread start, an APC, a vectored handler
and a Cygwin signal handler, and a fault beneath a SysV frame came back as
SIGSEGV and left by `siglongjmp`. What remains unmeasured is unwind data --
`RtlUnwindEx` through a `sysv_abi` frame, and C++ exceptions crossing at all --
`DllMain` and PE TLS callbacks into a DLL whose export surface is SysV, and
Cygwin's source rebuilt rather than called.

That the red zone is Windows' to break. It is not, and that is the shape of
the finding rather than a detail of it. Measured 2026-08-29: preemption and
thread hijacking leave the 1024 bytes below `%rsp` untouched, and Windows' own
exception dispatch starts writing about 320 bytes down, well outside the 128
the psABI reserves. Cygwin's signal delivery takes `%rsp-8` first, every time.
So `-mno-red-zone` is required by our own layer, and gcc does give a
`sysv_abi` leaf a red zone here, so the flag changes code rather than
restating a default.

The licenses marked recalled: flinux, Blink, Qiling, Cosmopolitan, HelloElf,
elf-on-windows. Each must be read before any code is lifted, and GPL or LGPL
turns a lift into a distribution obligation.

Path length under the longer vendor: the deepest installed path carrying a
triple measures 146 characters as Windows sees it, 156 with the ten the vendor
adds, against a `MAX_PATH` of 260. Measured 2026-08-20.

That el8 binaries carry 2 MB PT_LOAD alignment. Recalled binutils default,
settled by one readelf against a vendor binary. Spike 2 showed the mapping
works either way, so the answer now sizes the spans rather than deciding the
method.

That anything in a PE stub runs early enough to reserve a non-PIE image's span
before the runtime under it allocates. Spike 2 established that something has
to and named a TLS callback and the image entry point as the candidates.
Neither has been tried.

Gramine and Graphene as a design reference rather than a source. Cited for the
libc-redirected-syscall pattern, not read line by line today.

Blink building and running under this tree's pinned 2019 Cygwin. Its README
claims Cygwin support in general; nothing was reproduced here.

That no project combines a native-execution ELF loader with a Cygwin-backed
runtime. A search result from today, not a proof of absence.
