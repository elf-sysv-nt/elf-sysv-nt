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
ones from the second column to the first.

## Relationship to rhelcyg-8.10

`rhelcyg-8.10` builds a RHEL 8.10 userland on the Cygwin runtime, and it is the
first consumer of this project rather than its parent. It consumes this the way
it consumes Cygwin today: an external dependency with a version. The split
exists because everything below the kernel-ABI seam is platform rather than
packaging, and because an el9 or el10 effort would sit on the same platform
with a different veneer.

## Status

Pre-spike. No toolchain, no loader, no DLL, no code of any kind.
