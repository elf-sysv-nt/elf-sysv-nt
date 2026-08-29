# ELF through a user-space loader, survey and assessment

Surveyed 2026-08-20 in `rhelcyg-8.10` at commit `54a8fc8`, moved here the same
day. This answers the open question that repository's working notes left on
image format and symbol versioning, and it scopes
`symbol-versioning-formats.md` so that record is not read more broadly than it
holds. It is the survey behind `elf-technical-breakdown.md`, and where the two
disagree the breakdown is later and wins; the one place they do disagree is
flagged below.

Nothing here was measured. Most of it rests on sources read that day, cited at
the end, and the spikes near the bottom exist to change that before anything
gets built on it.

## The finding

The impossibility record is true as written and narrower than it reads. Its
claim is that the Windows user-mode loader executes exactly one format;
nothing below disputes that. A libc `exec*()` that recognizes ELF magic and
maps the image itself never asks that loader for anything: Windows sees a
PE host stub, and the ELF file is data. Every piece of the design exists in
prior art. No one has assembled it at production scale. Of its two
variants, one is priced out by the same syscall wall
`rhelcyg-8.10`'s `doc/plan-rpm-userland.md` already recorded, and the other
is expensive in engineering-years rather than impossible; the affordable one
repairs nearly everything the record lists as permanent cost, versioned
Provides and Requires included. Whether that is worth a multi-year runway is a
program decision, not a technical finding.

The stated concession holds. Such executables are opaque to Windows tools:
a debugger or a dependency walker sees a stub plus anonymous executable
regions. That is Wine's situation inverted, and Wine's remedy, owning the
debug channel, transfers.

## Why exec is the right seam

exec on this platform is already a libc fiction. Cygwin's `execve` cannot
replace a Windows process image; it creates a new process and leaves the
parent behind as a stub, and its spawn path already dispatches `#!` scripts
off magic bytes. Linux keeps the same dispatch in the kernel and calls it
`binfmt_misc`. Moving it into libc changes where the dispatch table lives,
nothing else, so "a highly customized exec" names a small extension to a
mechanism that was never the native one to begin with.

## Prior art, sorted by what it proves

**Loading is the easy part, proven repeatedly.** LBW (David Given, 0.1
released 2010-04-01) carried its own ELF loader and handed shared objects
to Linux's real `ld.so`; it ran on 32-bit XP over Interix, and it died when
Interix did ("Microsoft have killed Interix" is the author's own verdict).
Cosmopolitan's APE loader maps its payload itself on several systems, and
the grugq's userland exec made the primitive standard long before either.
Mapping PT_LOAD segments and building an auxv stack is a few thousand lines
of well-trodden code.

**The Linux kernel ABI is the hard part, also proven repeatedly.** On
32-bit Windows an `int 0x80` faults, and a handler can catch the fault,
which is what the LINE-era projects rode; on x86-64 the `syscall`
instruction enters the NT dispatcher directly, with no sanctioned user-mode
trap, so every 64-bit attempt becomes a binary translator (recalled, for
the 32-bit half). flinux (wishstudio) did exactly that: dynamic binary
translation plus a Linux syscall layer, pure user mode, no drivers. It ran
static and dynamic i386 binaries, sockets and a terminal included, then
stalled with signals, multithreading, process management and permissions
still on the missing list; the repository has been dormant for years.
HelloElf (trungnt2910) is the modern x86-64 retry, a loader feeding a
Zydis-based translator with a syscall handler beneath, described by its own
author as an attempt. Blink (jart) goes the whole way: a ~220 KB emulator
implementing about 600 instructions and about 180 Linux syscalls with a
JIT, whose README lists Cygwin among its build hosts. It works, at
emulation cost. qemu's linux-user mode does not support Windows hosts at
all. WSL1 and coLinux solved the same problem below user mode, one as a
Microsoft-signed pico provider and one as a ring-0 cooperative kernel;
neither route is open to this program, for the reasons already in the
impossibility record.

**Two production-scale proofs, one on each side of the mirror.** Wine is
this proposal reflected: a user-space loader for a foreign format on a host
whose native tools cannot see inside the result, which is why Wine grew its
own debugger support; its SysV-to-MS-x64 boundary is crossed with
`ms_abi`-annotated thunks that gcc emits on request. midipix is the
instructive contrast. It puts a full POSIX layer on NT by satisfying musl's
syscall surface (the mmglue port), it remains active as of a 2026-02-15
build-logic update, and it chose PE as its object format deliberately,
which is exactly how it inherited PE's versioning limits. Nobody has yet
combined midipix's runtime idea with Wine's loader idea. That absence is a
search result rather than a finding.

## The two designs, and the one that survives

Design (a) runs unmodified el8 binaries. That requires the Linux kernel
ABI in user space: binary translation for syscall sites, fork, clone,
futex, FS-based TLS, /proc, signal semantics. It is flinux's grave, and
el8's glibc 2.28 exercises far more of that surface than the busybox-class
binaries flinux managed. `rhelcyg-8.10`'s `doc/plan-rpm-userland.md` rejected
the syscall route once already; (a) is that rejection ignored. Priced out.

Design (b) makes ELF this tree's native object format with our runtime
underneath. Packages rebuild, which the program does anyway, with a
toolchain targeting ELF; the customized `exec*()` spawns a PE host stub
carrying our `ld.so`; every library call bottoms out in `cygwin1.dll`
through an `ms_abi` shim. No translation, native speed, no kernel
involvement in anything versioning touches.

That last clause is the one place this document is out of date, and the
correction is the whole reason the project has a name. The `ms_abi` shim sat in
the libc veneer, one thunk per glibc export, which put the convention change in
the widest layer available. It has since moved down: the runtime is rebuilt as
`elfsysv1.dll` with a System V export surface, the ELF world above is uniformly
SysV, and the veneer collapses into versioned aliases. Read
`elf-technical-breakdown.md` for the placement and its cost, which is a DLL that
must be bilingual inside.

Everything symbol versioning needs runs in user space.

The kernel's entire role in executing a Linux binary is mapping the
PT_LOAD segments of the file and of its PT_INTERP interpreter, then
building the initial stack and jumping; `.gnu.version_d`,
`.gnu.version_r`, and the load-time "version not found" refusal all live
in `ld.so`. A loader we write honors them because we wrote it.

## What design (b) repairs

Measured against the two existing documents, nearly everything.

The toolchain trap dissolves: `--version-script` and `.symver` work
because the target is ELF, so the silent-filtering hazard recorded for PE
never arises. `file` reports ELF, rpm's magic gate matches, and `elfdeps`
fires unmodified; Provides and Requires take the vendor's exact
`libc.so.6(GLIBC_2.2.5)(64bit)` shape, which repairs what the record calls
a permanent structural difference on nearly the whole package set. A shim
`libc.so.6` can export versioned symbols under el8-shaped `GLIBC_2.28`
nodes that forward into Cygwin, so `memcpy@GLIBC_2.2.5` becomes
expressible rather than unrepresentable. SONAME, RPATH, RUNPATH,
`ld.so.conf` and `LD_LIBRARY_PATH` become real loader features, which
retires the PATH condition, the NT-symlink condition and the
import-library condition from the library-naming spike, along with the
five-variable libtool correction.

One closure stands. glibc itself demands a Linux kernel personality
underneath, the same wall in the same place, so the C library remains
newlib plus Cygwin, now wearing a glibc-shaped, versioned ABI surface.

## What design (b) costs

fork. ELF mappings must replay in the child. Routing every loader mapping
through Cygwin's own `mmap` puts them inside the machinery Cygwin's fork
already replays; and since ELF PIC objects relocate anywhere, the DLL
rebase failure mode gets removed rather than added to. Fiddly, not novel.

TLS was the load-bearing unknown and it is now a measured no. FSGSBASE
(`wrfsbase` and friends) has been in every processor since Ivy Bridge and
Windows permits the instruction, so the hardware half of the question was
never the problem; what decided it is that Windows hands a descheduled
thread back with the base at zero, preemption included, which spike 1
measured on 2026-08-29. ELF-standard TLS at native cost is therefore
unavailable. The fallback costs little, because we own the target, and it
has been chosen: DR-0003 took carrier C3 of the gs-thread-pointer spike, a
runtime-owned thread pointer through `%gs` kept below the stack base in the
`_my_tls` shape, measured at about 5.5 cycles an access against emutls's 34.

The ABI boundary is real but solved. ELF x86-64 code is SysV; cygwin1.dll
is MS x64; gcc emits both conventions in one object via `ms_abi` and
`sysv_abi` attributes, and Wine crosses an equivalent boundary in
production, and spike 3 crossed it here on 2026-08-29 with every callee-saved
register intact in both directions. The red zone is the sharp edge: SysV leaf
code uses 128 bytes below rsp, and something does not honor them. Which
something was recalled wrongly and is now measured -- Windows leaves them
alone, and Cygwin's own signal delivery takes `%rsp-8` first. Either way the
ELF world compiles with `-mno-red-zone`, one line in the macro set since every
package rebuilds anyway. Hand-written assembly inside packages that
assumes a red zone is the residual risk, and it wants a ledger row.

The remainder is operational. Cygwin delivers signals by hijacking the
thread; the trampoline that lands on an ELF-side stack is ours to write,
workable because both sides are ours. DWARF unwinding stays inside the ELF
world and SEH inside the PE world; neither crosses the shim raw, the same
rule Wine enforces. Windows' 64 KB allocation granularity against 4 KB ELF
segment alignment is absorbed by reserve-and-commit in one region, easier
still if el8 binaries carry the linker's 2 MB max-page-size default
(recalled; one readelf against a vendor binary settles it), at the price
of losing cross-process text sharing. Self-mapped anonymous executable
memory is malware-shaped, so enterprise EDR will want exclusions,
permanently. CET shadow stacks and CFG get opted out in the stub. And
debugging is the conceded opacity: implement the SVr4 `r_debug` rendezvous
and a gdb built for the triple sees everything, while Windows tools see
the stub.

## Scale, honestly

The loader is small. musl's entire `dynlink.c` is on the order of four
thousand lines, and musl's versioning support is deliberately partial, so
glibc's resolver is the reference; a correct verdef and verneed matcher is
hundreds of lines on top of a basic relocator. The binutils and gcc target
is routine cross-toolchain work. The long pole is the libc glue between
the ELF world and `cygwin1.dll`, midipix-shaped effort minus the part
midipix had to write from nothing, because the syscall layer here already
exists and ships as a DLL.

One design decision wants making early: the triple. Masquerading as
`x86_64-*-linux-gnu` maximizes configure-time fidelity and invites probes
to find epoll and inotify missing at link time; an honest custom triple
tells the truth sooner and diverges from vendor configure results. Probes
link and run either way, so the truth comes out. Choose where.

## Spikes, in order

1. FS base persistence. Write FS base with `wrfsbase`, provoke everything
   that can take the thread off a processor, read back, on this Windows
   build. An afternoon, and it decided the TLS story. Run 2026-08-29: no.
2. Map and jump. A PE stub maps a static ELF hello built for the custom
   target, then jumps to it across a hand-built initial stack and auxv.
   Proves the exec path end to end with no dynamic linking involved.
3. The crossing. An ELF-side object calls a `cygwin1.dll` export through
   an `ms_abi` thunk; deliver a signal mid-call and inspect the 128 bytes
   below rsp. Settles the red-zone claim by measurement. Run 2026-08-29:
   yes on the crossing, and the red zone goes to Cygwin's delivery rather
   than to Windows. `spike/abi-crossing/`.
4. The payoff check. Synthesize a shim `libc.so.6` carrying one verdef
   node, run `elfdeps` from el8's rpm against a consumer linked to it, and
   confirm the vendor-shaped Requires line appears. Proves the fidelity
   repair before anything large is funded.

## What to record

Nothing here overturns the impossibility record; it fences it. That record
should gain one sentence: a user-space loader inside exec is not covered
by the impossibility and is priced separately, see this note. If the
program declines the price, the accepted deviation stands exactly as
drafted. If it takes the price, this stops being a note and becomes a
stage with its own document, and the spike results above go into it first.

Resolved the same day. The price was taken, and it became a repository rather
than a stage: this one. The fence belongs on the record either way, and
`rhelcyg-8.10` gains a dependency row rather than a stage.

## Sources

Read 2026-08-20 unless noted.

User-space attempts

    https://cowlark.com/lbw/index.html
    https://github.com/wishstudio/flinux
    https://github.com/wishstudio/flinux/wiki/Dynamic-Binary-Translation
    https://github.com/trungnt2910/HelloElf
    https://github.com/byronwanbl/elf-on-windows
    https://github.com/jart/blink
    https://github.com/qilingframework/qiling
    https://www.qemu.org/docs/master/user/main.html

The two production proofs

    https://www.midipix.org/
    https://github.com/midipix-project/mmglue

Mechanism references

    https://gist.github.com/merryhime/f22e75d5128c07d77630ca01c4272937

In-tree: `symbol-versioning-formats.md`, `elf-technical-breakdown.md`.

In `rhelcyg-8.10`: `doc/plan-rpm-userland.md`, `doc/stage-model.md`, the two
`spike/*/results-2026-08-19.txt` transcripts, and the working notes on image
format and symbol versioning that prompted this survey.

## Not verified

Recorded so a later reader does not mistake these for measured.

Which TLS model stands in for `%fs`. Settled, no longer an open line. Whether
Windows preserves a user-written FS base was measured no on 2026-08-29
(`spike/fs-base-persistence/`); the gs-thread-pointer spike then measured the
replacements and DR-0003 took carrier C3, a runtime-owned thread pointer through
`%gs`. What is left to verify is the stand-in the spike used against Cygwin's
real `_my_tls`, which WP-30 does as it builds.

That Windows exception and APC dispatch clobbers the SysV red zone. Recalled
from Wine-adjacent reading, measured by spike 3 on 2026-08-29, and wrong:
Windows' exception dispatch starts about 320 bytes below `%rsp` and preemption
and thread hijacking write nothing at all. Cygwin's own signal delivery is what
takes `%rsp-8`, on every delivery. The `-mno-red-zone` conclusion survives the
correction; the attribution does not.

el8 binaries carrying 2 MB PT_LOAD alignment. Recalled binutils default,
checkable with one readelf against any vendor binary.

flinux's abandonment. Inferred from repository dormancy; its README still
says heavy development. No author statement was found either way.

Blink building and running under Cygwin. Its README's claim, not
reproduced here, and never against this tree's pinned 2019 Cygwin.

That no project combines a native-execution ELF loader with a
Cygwin-backed runtime. A search result from today, not proof of absence.

The 32-bit fault-and-catch mechanism for `int 0x80` as what LINE-era
projects used. Recalled; those projects are dead and their sources were
not read today.
