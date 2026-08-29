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

The spikes are done and nothing else is started. No toolchain, no loader, no
DLL, and no code beyond the probes the spikes are made of. All five ran on
2026-08-29. Spike 1 found that Windows does not preserve a user-written FS
base, which takes `%fs`-relative TLS off the table and leaves the replacement
open. Spike 2 mapped a static ELF from a PE stub and entered it, with a
constraint on when the image's span has to be claimed. Spike 3 crossed the ABI
boundary in both directions and found the red zone destroyed by Cygwin's own
signal delivery rather than by Windows. Spike 4 got el8's `elfdeps` to read a
vendor-shaped `Requires` off a synthesized `libc.so.6`, byte for byte, which is
the whole point of the exercise demonstrated in miniature. Spike 5 priced the
target triple at one affected package in 2893.

What is open is the TLS model, which is the operator's to pick, and the choice
between two repairs for the red zone. `AGENTS.md` reserves both.
