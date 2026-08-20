# Symbol versioning, and whether another image format could carry it

Surveyed 2026-08-20 in `rhelcyg-8.10`, against the open item in that
repository's `doc/stage-model.md`, and copied here the same day. This is the
record this project exists to fence, and the sections about rpm, the fidelity
test and the stage model are that repository's concern rather than this one's.
Read it for the format findings, which are why ELF is worth the price.

**Scope.** The impossibility below is about the Windows user-mode loader, and
only about it. A user-space loader inside `exec*()` maps the ELF image itself
and never asks that loader for anything, so it falls outside this record and is
priced separately in `elf-userspace-execution.md`. Nothing here is overturned by
that route; the fence is the whole point of stating it this way.

No. There is exactly one image format the Windows user-mode loader executes,
its export table has no version field, and no alternative format is reachable.
The impossibility should be recorded rather than worked around, and it costs
more than the open item suggests: it closes glibc as well, and it puts a
permanent structural difference into the `Provides` and `Requires` half of the
fidelity test.

What el8 libraries carry, and what has to be given up, is the GNU scheme:
version definitions in `.gnu.version_d`, requirements in `.gnu.version_r`, the
`symbol@VERSION` and `symbol@@VERSION` forms, `--version-script` with a `.map`
file, and `GLIBC_2.x`-style nodes letting one library export two definitions of
one name so that old callers keep the old behavior.

## The formats

    format          Windows runs it?      symbol versioning   status
    PE32 / PE32+    yes, exclusively      no                  live
    POSIX / PSXSS   was PE                no                  dead 2013
    Interix, SUA    was PE                no                  dead 2013
    OS/2 NE         16-bit only           no                  dead ~2001
    ELF via WSL1    kernel, not loader    yes, inside Linux    frozen
    ELF via WSL2    virtual machine       yes, inside Linux    live, irrelevant
    ELF, user-mode  proof of concept only no                  dead ends
    CLI assemblies  PE with metadata      assembly-level only  live, wrong grain
    WebAssembly     runtime, not loader   interface-level      not applicable

**PE is not one option among several.** The PE header's `Subsystem` field still
enumerates `IMAGE_SUBSYSTEM_OS2_CUI` and `IMAGE_SUBSYSTEM_POSIX_CUI`, which
reads as though NT once loaded other formats. It did not. Those values live
inside a PE header and select which subsystem services a PE image.

**The POSIX subsystem and Interix used PE, which is the finding that kills the
whole idea.** A classic POSIX-subsystem binary was built with
`link /SUBSYSTEM:POSIX` against `psxdll.lib`, and it is an ordinary Microsoft
PE whose only import is `PSXDLL.DLL`. Interix, later Services for UNIX and then
Subsystem for UNIX-based Applications, was a superset of that same environment
subsystem. A Microsoft engineer put the consequence plainly on the WSL team's
blog: with the old POSIX subsystem "you needed to actually recompile to get
things to work," which is the opposite of running unmodified Linux binaries.
Interix 6.1 shipped in Windows 7 and Server 2008 R2, was deprecated in Windows
8, and was removed in Windows 8.1 and Server 2012 R2. So the most promising
sounding alternative was PE all along, and has been gone for thirteen years.

**OS/2.** NT up to and including Windows 2000 carried an OS/2 subsystem for
character-mode 16-bit OS/2 applications, x86 only, in NE format. Removed after
2000. I found no evidence NT ever executed 32-bit LX binaries, and that is a
negative from searching rather than a documented statement.

**WSL1 does execute ELF, and the mechanism is the reason it cannot help.**
`lxcore.sys` is a pico process provider: a kernel driver that owns a minimal
process with no `ntdll.dll`, no PEB, and no initial thread, so the Windows
loader is bypassed by design rather than extended. Microsoft's May 2025
open-sourcing of WSL explicitly excluded `Lxcore.sys`, which remains closed.
`PsRegisterPicoProvider` is not in the WDK reference, and a pico provider must
be a core driver signed with a Microsoft certificate that loads before
third-party drivers. Windows has no `binfmt_misc`. There is no extensibility
point through which a project could teach `CreateProcess` a new image format.

**WSL2** runs a real Linux kernel in a lightweight virtual machine. ELF works
there because Linux is there. Nothing about the Windows loader is involved.

**User-mode ELF loaders for Windows** are all proofs of concept.
`byronwanbl/elf-on-windows` is abandoned by its author's own statement;
`VLiance/XE-Loader` is experimental; most GitHub projects matching the search
either run on Linux or load ELF relocatable objects in the BOF style rather
than shared libraries. LBW came closest, loading ELF and delegating `.so`
handling to Linux's own `ld.so`, and it depended on Interix, ran only on 32-bit
Windows XP, and its author declared it dead: "Microsoft have killed Interix."
None documents `.gnu.version_d` support.

Cosmopolitan's APE is worth naming because it gets misread as ELF on Windows.
It is a polyglot file that Windows interprets as a Portable Executable. Windows
is running PE.

**CLI assemblies** are PE files flagged by the COM descriptor data directory,
and their versioning is real but at the assembly grain: all versioning of
assemblies is done at the assembly level, in Microsoft's own words. Modern .NET
never validates the strong-name signature for binding, `<bindingRedirect>` is
.NET Framework only, and a single `AssemblyLoadContext` is limited to one
version per simple assembly name. None of it reaches native code anyway.
P/Invoke resolves a native library purely by file name, with no version
predicate and no per-export selector, and a mixed-mode C++/CLI assembly has its
native exports resolved by the ordinary import and export tables. It cannot
express `realpath@GLIBC_2.2.5` and `realpath@@GLIBC_2.3` in one library.

## What PE's export table actually holds

The export directory table's fields, from the specification: export flags
(reserved, zero), a time stamp, major and minor version, name RVA, ordinal
base, address table entries, number of name pointers, and RVAs for the export
address table, the name pointer table and the ordinal table.

Two of those look like a version and are not. Major and minor are per-DLL, the
spec saying only that they can be set by the user; in GNU `ld` they come from a
`VERSION` statement in a `.def` file and land at `edata_d + 8`. The name pointer
table is an array of pointers to export name strings sorted ascending. Nothing
there can hold two definitions of one name. The import side is symmetric: an
import lookup entry is an ordinal or an RVA to a two-byte hint plus a name
string, with no version either.

Forwarders are the one indirection PE offers, and they are a rename table.
An export address entry pointing inside the export section is a forwarder RVA
naming a target as `MYDLL.expfunc` or `MYDLL.#27`, which is how `HeapAlloc` in
`Kernel32.dll` reaches `NTDLL.RtlAllocateHeap`. One name goes to one target.
There is no way to send different callers of the same name to different places.

Windows does solve DLL versioning, at a coarser grain, four different ways.
Side-by-side assemblies and activation contexts bind a whole assembly by
identity from a manifest, which is how Comctl32 v5 and v6 coexist; the unit is
the DLL, and two versions in one process are two images with separate state.
API sets make a name like `api-ms-win-core-file-l1-1-0` a virtual alias for a
physical DLL, so the version digits sit in the contract name and exactly one
implementation is live. Versioned filenames, `MSVCR100.dll` or
`cygncurses-10.dll`, are the soname bump, which is the coarse mechanism symbol
versioning exists to avoid. COM versioning is a convention over vtables plus a
GUID and an activation lookup, where changing a method requires an entirely new
interface, and it validates nothing at link time; expressing a POSIX C API of
free functions that way means rewriting every caller.

I found no proposal or implementation adding symbol versioning to PE in
binutils, lld, MinGW-w64 or Cygwin. That is "none found," not proof of absence.
The nearest authoritative statement is Martin Storsjö's, on the lld MinGW
driver: for ELF, version scripts select what to export and set versions, and
"as COFF doesn't have symbol versions," on mingw with GNU ld all it does is
filter symbols.

## The trap the toolchain sets

This is the operational half, and it matters more day to day than the
impossibility does.

GNU `ld` accepts `--version-script` on PE targets. It does not error, it does
not warn, and it honors only the `local:` and `global:` visibility filtering
while discarding every version name. The current ld manual states it: the
option is fully supported on ELF, and "partially supported on PE platforms,
which can use version scripts to filter symbol visibility in auto-export mode."
The implementation is one condition inside
`process_def_file_and_drectve()` in `ld/pe-dll.c`, calling
`bfd_hide_sym_by_version` on a symbol that would otherwise be exported.

Three consequences, and the first is the dangerous one.

An autoconf or CMake probe asking "does the linker accept `--version-script`?"
passes on Cygwin, and then produces a completely unversioned DLL. A package
that checks and adapts will conclude versioning is available. Nothing fails,
nothing logs, and the resulting library is wrong in a way that only shows up
when two versions of a symbol were supposed to exist.

The filtering applies in auto-export mode only. Per the ld WIN32 chapter,
auto-export is disabled by a `.def` file or by any `__declspec(dllexport)`, in
which case the version script does nothing whatever.

Version node names, dependency trees, the `@` and `@@` default marking, and
`.gnu.version_r` requirements are all discarded. Nothing reaches `.edata`.

On the assembler side there is no escape hatch either. The `as` manual is
explicit that `.symver`, the directive that binds a symbol to a version node in
source, is supported on ELF platforms only. LLVM stopped passing its version
script on MinGW builds for this reason, and lld's MinGW driver still has no
`--version-script` option.

## What Cygwin does instead

`cygwin1.dll` does not version symbols. Its export list, `winsup/cygwin/cygwin.din`,
is a plain DEF file: a flat `EXPORTS` list with `DATA` markers, `SIGFE` and
`NOSIGFE` signal-frame annotations, and `=` aliases such as
`atexit = cygwin_atexit`. No ordinals, no version syntax.

The compatibility mechanism is a single whole-DLL counter checked at runtime.
`CYGWIN_VERSION_API_MAJOR` and `CYGWIN_VERSION_API_MINOR`, currently 0.357,
increase monotonically with a hand-maintained changelog of every API addition;
each process records the API version it was built against, and the DLL branches
on it through macros like `CYGWIN_VERSION_CHECK_FOR_EXTRA_TM_MEMBERS`. That
gates behavior inside a function. It cannot hand two callers two function
bodies bound at link time, which is the thing symbol versioning does.

Corinna Vinschen, on the same territory in June 2016: RPATH and RUNPATH are ELF
dynamic loader features, not supported by PE/COFF, and implementing the full
set would take major effort. Elsewhere on the list, the maintainers record
having discussed writing their own dynamic loader and given up on workload.

I could not find any documented cygport handling of `.map` files or version
scripts, and I think the reason is the section above: nothing needs handling,
because nothing fails.

## What this costs the fidelity test

More than a ledger row, and the connection runs through rpm's dependency
generator.

A requires of the form `libc.so.6(GLIBC_2.2.5)(64bit)` is produced by
`elfdeps`, driven by rpm's internal generator through the file-attribute
mechanism. `fileattrs/elf.attr` binds `%__elf_requires` to
`elfdeps --requires`, gated by a magic pattern matching `file(1)` output that
begins `ELF (32|64)-bit`. Inside, `processSections()` dispatches on section
type: `SHT_GNU_verneed` to `processVerNeed()`, `SHT_GNU_verdef` to
`processVerDef()`, both walking version records and calling `addDep()`, which
formats `"%s(%s)%s"` with a `(64bit)` marker for `ELFCLASS64`. `glibc` supplies
the matching provides, `libc.so.6(GLIBC_2.2.5)(64bit)` among hundreds.

There is no PE equivalent anywhere in rpm. The `fileattrs/` directory on master
holds `debuginfo, desktop, elf, font, metainfo, ocaml, pkgconfig, rpm_lua,
rpm_macro, script, sysusers, usergroup`, and no `pe.attr` or `coff.attr`. The
magic pattern never matches a PE file, so no generator fires.

Worse for the immediate work: Cygwin's `rpm-4.18.0-1` ships no `elfdeps`, no
`fileattrs/`, and no `*.attr` files at all, while `rpm-build-4.18.0-1` ships
`find-provides` and `find-requires` as 91-byte stubs. The package is ORPHANED,
and cygwin.com describes it as useful for cross-building RPM packages for
another OS. Stage 0 therefore has no automatic binary dependency extraction of
any kind.

So the `Provides` and `Requires` gate needs a generator written before it can
run at all, and even a well-written one has a hard ceiling. It can emit
module-level requires against a DLL name. It can never emit a version node,
because the file does not contain one. Every el8 package linking libc carries
requires of that shape, so the comparison differs from the vendor structurally
and permanently, on nearly the whole package set.

## What to record

One accepted deviation, worded at the level it actually holds, since a row per
package would be four hundred rows saying the same thing. Symbol versioning is
unavailable because PE has no representation for it and no executable
alternative format exists on Windows; therefore versioned `Provides` and
`Requires` cannot be generated or satisfied, and the metadata gate compares
module-level dependencies only.

One closure, in `doc/plan-rpm-userland.md` beside the syscall rejection. glibc
is out for the same reason and by consequence: it ships
`ld-linux-x86-64.so.2`, which is the ELF dynamic loader, so closing ELF closes
glibc. The C library is newlib plus Cygwin, permanently, and `glibc`,
`glibc-devel` and `glibc-headers` are unbuildable at any price.

One warning in `AGENTS.md`, for the trap rather than the impossibility. A
configure probe for `--version-script` passes on this platform and produces an
unversioned library, so a package whose build system adapts on that answer is
silently wrong, and the linker will never say so.

One stage 0.5 admission: a PE dependency generator. It satisfies all four
clauses of the rule in `doc/stage-model.md`, since stage 1 cannot be compared
correctly without it, no index package supplies it, it would be ours with the
source committed here, and its absence gets papered over per package otherwise.

## Sources

PE and Windows loader

    https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
    https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets
    https://learn.microsoft.com/en-us/windows/win32/sbscs/about-side-by-side-assemblies-
    https://learn.microsoft.com/en-us/windows/win32/com/interface-design-rules

Historical subsystems

    https://blog.ret2.io/2017/09/20/subsystem-posix/
    https://en.wikipedia.org/wiki/Interix
    https://www.os2museum.com/wp/nt-and-os2/
    https://cowlark.com/lbw/

WSL

    https://learn.microsoft.com/en-us/archive/blogs/wsl/pico-process-overview
    https://blogs.windows.com/windowsdeveloper/2025/05/19/the-windows-subsystem-for-linux-is-now-open-source/
    https://learn.microsoft.com/en-us/windows/wsl/compare-versions

Toolchain behavior

    https://sourceware.org/binutils/docs/ld/Options.html
    https://sourceware.org/binutils/docs/ld/WIN32.html
    https://sourceware.org/binutils/docs/as/Symver.html
    https://sources.debian.org/src/binutils/latest/ld/pe-dll.c/
    https://reviews.llvm.org/D63743

Cygwin

    https://cygwin.com/pipermail/cygwin-developers/2016-June/011550.html
    https://cygwin.com/cygwin-ug-net/dll.html
    https://cygwin.com/packages/x86_64/rpm-build/rpm-build-4.18.0-1
    winsup/cygwin/cygwin.din, cygwin/version.h

rpm

    https://raw.githubusercontent.com/rpm-software-management/rpm/rpm-4.18.2-release/tools/elfdeps.c
    https://raw.githubusercontent.com/rpm-software-management/rpm/rpm-4.18.2-release/fileattrs/elf.attr

## Not verified

Recorded so a later reader does not mistake these for measured.

No proposal to add symbol versioning to PE was found in binutils, lld,
MinGW-w64 or Cygwin. That is a search result, not a proof.

Whether NT ever ran 32-bit OS/2 LX binaries. Evidence exists only for 16-bit
character-mode OS/2 1.x.

WSL1's status in 2026. The last authoritative statement found is Microsoft's
May 2025 "which WSL still supports." No formal deprecation and no 2026-dated
statement either way.

Whether Interix supported anything beyond PE DLLs for its shared libraries.

Whether cygport has any codified policy on version scripts. Nothing documented
was found, and the practical answer appears to be that none is needed because
`ld` degrades silently.

Whether el8's own rpm 4.14.3 build carries `elfdeps` and `fileattrs`. The
Cygwin port omits them; whether that omission is the port's or the version's is
untested, and it is the first thing to probe.
