# elf-sysv-nt

A Cygwin-derived userland kernel that speaks ELF and System V outward and
Windows NT inward, so that a Linux userland can be built against it largely
unchanged.

The premise is narrow and the consequence is not. Windows executes exactly one
image format, and that format cannot carry ELF symbol versioning; the record of
why sits in `doc/symbol-versioning-formats.md`. But a `exec*()` that recognizes
ELF magic and maps the image itself never hands the file to the Windows loader,
so the limit applies to a loader this project does not use. Above that seam the
world is ELF, System V, and versioned. Below it, one DLL descends into `ntdll`
and `kernel32` the way `cygwin1.dll` already does.

The processor is the reason this is worth attempting at all. A Linux binary and
a Windows binary run on the same silicon, with the same registers and the same
instruction encodings, so nothing here translates instructions. The gap opens
only where a binary stops computing and asks the operating system for
something: a mapping, a thread, a signal, a symbol resolved at load time.
Supplying those from user space, on top of the Cygwin runtime, is the whole
job.

## Where to start reading

`doc/elf-technical-breakdown.md` is the design, laid out leaf to trunk, with
each layer marked by how much of its foundation already exists in public code.
`doc/elf-userspace-execution.md` is the survey behind it: the prior art sorted
by what it proves, and the two designs priced against each other.
`doc/milestones.md` is what happens first, which is five spikes and no code.
`doc/decisions/` is what has been settled, and `doc/proposals/` is the argument
each settlement came out of.
`doc/ROADMAP.md` is everything that has to be built after them, and
`doc/IMPLEMENTATION-PLAN.md` breaks that into work packages with entry and exit
criteria.

Nothing has been built. Every technical claim carries a mark saying whether it
was measured or recalled, and the spikes exist to move four of the load-bearing
ones from the second column to the first. All of them have moved, and the first
one moved against the design, which is the outcome a spike is worth having for.

## Relationship to rhelcyg-8.10

`rhelcyg-8.10` builds a RHEL 8.10 userland on the Cygwin runtime, and it is the
first consumer of this project rather than its parent. It consumes this the way
it consumes Cygwin today: an external dependency with a version. The split
exists because everything below the kernel-ABI seam is platform rather than
packaging, and because an el9 or el10 effort would sit on the same platform
with a different veneer.

## Status

Eight spikes have run, all on 2026-08-29, and phase 1 has started. There is no
loader and no DLL.

Spike 1 found that Windows does not preserve a user-written FS base, which took
`%fs`-relative TLS off the table. Spike 2 mapped a static ELF from a PE stub
and entered it, with a constraint on when the image's span has to be claimed.
Spike 3 crossed the ABI boundary in both directions and found the red zone
destroyed by Cygwin's own signal delivery rather than by Windows. Spike 4 got
el8's `elfdeps` to read a vendor-shaped `Requires` off a synthesized
`libc.so.6`, byte for byte, which is the point of the exercise in miniature.
Spike 5 priced the target triple at one affected package in 2893. Spike 6
measured four `%gs` carriers for the thread pointer `%fs` could no longer hold,
and DR-0003 took one. Spike 7 showed a signal delivery that reserves the red
zone before building its frame keeps it whole. Spike 8 found that an access
through a zeroed FS base faults rather than reading, and that a handler can
resume from it, which is what allows a load-time rewriter for vendor binaries
to be a heuristic rather than exhaustive.

In phase 1, the target definition is settled in `doc/target-definition.md` and
`config.guess` names the vendor. That record now also carries what the triple's
`linux` and `gnu` fields claim: `gnu` is glibc exactly, and `linux` is the
Linux kernel ABI satisfied by rebuilding against our runtime rather than by
dispatching system calls, which bounds it at the one axis where a raw `syscall`
instruction would need a kernel to reach. DR-0005 settles that wording and
leaves the triple itself alone. Binutils builds for the triple with no port at
all, and passes ten acceptance claims covering symbol versioning and the header
bytes. That package is reopened rather than finished: `ld` emits its own
`%fs`-relative thread pointer fetches, which nothing in the original criteria
caught.

What is open is the choice between two repairs for the red zone, which
`AGENTS.md` reserves, and two licence questions that DR-0004 reserves for
counsel.

## Licence

LGPLv3 or later. Inherited rather than chosen: this rebuilds Cygwin's `winsup`
library with a different export face, and Cygwin's own linking exception
excludes a library based on the Cygwin library by its own definition.
`doc/decisions/0004-license.md` carries the reasoning and the two questions
that are not an engineer's to answer, and `doc/licensing.md` states the
position in one page. No linking exception is granted here yet.
