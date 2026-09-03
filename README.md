# elf-sysv-nt

A Cygwin-derived userland kernel that speaks ELF and System V outward and
Windows NT inward, so that a Linux userland can be built against it largely
unchanged.

The premise is narrow and the consequence is not. Windows executes exactly one
image format, and that format cannot carry ELF symbol versioning. But an
`exec*()` that recognizes ELF magic and maps the image itself never hands the
file to the Windows loader, so the limit applies to a loader this project does
not use. Above that seam the world is ELF, System V, and versioned. Below it,
one DLL descends into `ntdll` and `kernel32` the way `cygwin1.dll` already
does.

The processor is the reason this is worth attempting at all. A Linux binary and
a Windows binary run on the same silicon, with the same registers and the same
instruction encodings, so nothing here translates instructions. The gap opens
only where a binary stops computing and asks the operating system for
something: a mapping, a thread, a signal, a symbol resolved at load time.
Supplying those from user space, on top of the Cygwin runtime, is the whole
job.

## Where to start reading

`doc/design/Architecture.md` is the design of record. It is current at every
commit, it states the values and constants an implementer needs rather than
pointing at where they were decided, and it is the one document to read first.
Six sub-documents carry the subsystems large enough to want their own file:

    doc/design/ABI-Boundary.md       the System V / Microsoft seam, per symbol
    doc/design/Symbol-Resolution.md  the lookup engine and the version matcher
    doc/design/Address-Space.md      the low window, placement, protection precision
    doc/design/Runtime-Crossing.md   how a process comes to host the faced runtime
    doc/design/glibc-reuse.md        what of glibc may be taken, and on what grounds
    doc/design/target-definition.md  the six values a shipped artifact carries

Around them, `doc/design/Requirements.md` says what the platform must do and
how anyone will know, and `doc/design/Verification-Plan.md` says what counts as
proof. `doc/design/licensing.md` states the licence position in a page.
`AGENTS.md` carries the conventions, the three decisions reserved to the
operator, and where autonomy stops.

`doc/design/decisions/` is what has been settled, one record per file,
append-only: reversing one means a new record pointing back, never an edit.
`doc/design/proposals/` is the argument each settlement came out of. A governed
section that a record settled ends in a line naming the records, and
`bin/check-design-links` fails if an in-force record is cited nowhere or a
citation names a record something else has replaced.

`doc/history/elf-technical-breakdown.md` is the founding survey the design grew
out of, and `doc/history/elf-userspace-execution.md` is the survey behind that.
Both are kept as the reasoning that opened the project rather than as
statements of what the system is; where they and `doc/design/Architecture.md`
disagree, the architecture is current.

`doc/milestones.md` is the spike record, `doc/ROADMAP.md` is what has to be
built, and `doc/IMPLEMENTATION-PLAN.md` cuts that into work packages with entry
and exit criteria.

## Status

Forty-five work packages have landed and thirty-two spikes have run. There is a
loader, a faced DLL, a veneer, and an acceptance harness that gives a real
per-package verdict. What there is not yet is a package that builds, runs, and
passes its own test suite, which is the criterion everything else serves.

The spikes settled the load-bearing questions, and one of them went against the
design. Windows does not preserve a user-written FS base, which took
`%fs`-relative TLS off the table and moved the thread pointer to a
runtime-owned word reached through `%gs`. The ABI boundary crosses in both
directions. The red zone survives Windows and was destroyed by Cygwin's own
signal delivery, which is repaired at the delivery site; `-mno-red-zone` was
scaffolding and is retired. The target triple was priced at one affected
package in 2893.

What is open is bring-up rather than design. The acceptance crossing still
builds a stub of the wrong shape, so a rebuilt package reaches a runtime whose
base reads zero; the shape it must take is settled and the placement question
in front of it is not yet measured. `doc/design/Runtime-Crossing.md` states
both.

Every governed document ends in a Not verified section naming what it rests on
that nobody has measured, and `doc/status/not-verified.md` collects them into
one page.

## Relationship to rhelcyg-8.10

`rhelcyg-8.10` builds a RHEL 8.10 userland on the Cygwin runtime, and it is the
first consumer of this project rather than its parent. It consumes this the way
it consumes Cygwin today: an external dependency with a version. The split
exists because everything below the kernel-ABI seam is platform rather than
packaging, and because an el9 or el10 effort would sit on the same platform
with a different veneer.

## Licence

LGPLv3 or later. Inherited rather than chosen: this rebuilds Cygwin's `winsup`
library with a different export face, and Cygwin's own linking exception
excludes a library based on the Cygwin library by its own definition.
`doc/design/licensing.md` states the position in one page,
`doc/design/decisions/0004-license.md` carries the reasoning, and
`doc/design/decisions/0037-the-linking-exception-carries-forward.md` records
that the linking exception carries forward with the modified library, on the
reading the existing Cygwin forks already operate on.

Lifting upstream code is cleared on licence text and recorded practice rather
than on counsel, which the project has decided it will not have.
`doc/design/glibc-reuse.md` works the commonest case through in full, because
the licence question and the coupling question get collapsed into one and the
collapsed version is wrong in both directions.
