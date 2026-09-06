# elf-sysv-nt

A Linux kernel personality for Windows NT, built at the `syscall` boundary: the
x86-64 Linux syscall table as kernel 4.18 defines it, presented to an el8
(Rocky Linux 8) userland, so that glibc 2.28 and the packages built on it run
on Windows with their constants, their structure layouts and their loader
untouched.

The boundary is the whole idea. A program built for el8 sees its kernel
through one interface, three hundred and thirty numbered entries reached by
one instruction; everything else it thinks it sees of the kernel arrives as
bytes through that interface. Emulating there means there is no second set of
constants anywhere in the process, no translation table, and a reference
implementation to certify against: the kernel is right when the Linux Test
Project and glibc's own tests say the same thing here as on el8. Emulating at
the C library's boundary, which is Cygwin's choice and was this project's
first arc, means reconciling every constant glibc exposes with somebody
else's, forever; that arc is retired and lives in a sibling repository.

One core is written once: the syscall table, the VFS over NTFS in WSL's
on-disk metadata format, the process and thread model, signals, epoll,
sockets over AFD, IPC. Beneath it sit two substrates that run user code, and
a deployment gets the one its environment allows. Substrate N runs user code
as NT threads in a host process that imports only `ntdll`, reached through a
gate published in the auxiliary vector because NT cannot trap `syscall`; it
needs the userland rebuilt with a toolchain that emits the gate call and a
`%gs` thread pointer, and it runs where no hypervisor is permitted. Substrate
H runs user code on Windows Hypervisor Platform vCPUs in one kernel process
per user, a page-table root per Linux process; it runs el8's shipped RPMs
unmodified, and it needs the hypervisor.

## Where to start reading

`doc/design/Architecture.md` is the map over the design of record, current at
every commit. It says what the shape is and points at the proposal section
that carries the detail:

    doc/design/proposals/0011-a-kernel-at-the-syscall-boundary.md   the design, accepted
    doc/design/proposals/0012-the-process-seam-and-the-shape-of-h.md   what 0011 left unsaid, accepted
    doc/design/Substrate-Interface.md   the nine calls between the core and a substrate
    doc/design/Substrate-N.md           the native substrate, built and certified
    doc/design/Core-Phase1.md           the first program through the core
    doc/design/Core-Phase2.md           files: the VFS, checked against Rocky 8 and WSL

Around them, `doc/design/Requirements.md` says what the platform must do and
how anyone will know, `doc/design/Verification-Plan.md` says what counts as
proof, and `doc/design/licensing.md` states the licence position in a page.
`AGENTS.md` carries the conventions, the decisions reserved to the operator,
and where autonomy stops.

`doc/design/decisions/` is what has been settled, one record per file,
append-only: reversing one means a new record pointing back, never an edit.
`doc/design/proposals/` is the argument each settlement came out of. A governed
section that a record settled ends in a line naming the records, and
`bin/check-design-links` fails if an in-force record is cited nowhere or a
citation names a record something else has replaced.

`doc/milestones.md` is the spike record: forty-seven measurements of what this
Windows does, each with a script that regenerates its transcript.
`doc/history/` holds what was true and is kept as reasoning: the founding
surveys, the veneer arc's retirement, and the veneer's address-space protocol.

## Status

Substrate N is built and certified against the nine-call bar. The core's
Phase 1 runs: a static program written to the gate's ABI is mapped, entered on
the psABI initial stack with its auxiliary vector, calls `write` and
`exit_group`, and prints `hello`, checked against the same program on Rocky 8.
Every other syscall returns `-ENOSYS`.

Substrate H is designed to the mechanism and unbuilt. Its shape was measured
before it was chosen: mapping guest memory is lazy and pins nothing, a
process may hold one mapped partition at a time, a page-table fork of a
576 MB process copies in under a millisecond, and a clone of a
partition-holding host runs but forks in five to eight.

Everything else, the VFS, the supervisor, fork, signals, terminals, sockets,
is designed in 0011 and unbuilt. The phase plan is 0011 § 18; what is open
is bring-up rather than design, and the questions still the operator's are
0012's open questions.

Every governed document ends in a Not verified section naming what it rests on
that nobody has measured.

## Licence

LGPL-2.1-or-later, chosen rather than inherited: the kernel takes no code from
Cygwin, and the licence is glibc's, which every program this kernel runs
already links against. `COPYING.LESSER` is the text,
`doc/design/licensing.md` the position, and
`doc/design/decisions/0095-the-licence-is-chosen-not-inherited.md` the
reasoning. Lifting upstream code is cleared on licence text and recorded
practice, per DR-0074, and GPL material (the Linux sources, LTP) is read as
specification and never lifted.
