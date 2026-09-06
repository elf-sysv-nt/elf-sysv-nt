# The process seam

What one Linux process cannot own, reached by the core as an interface and
realised twice. Proposal 0012 § 1 draws the line; `doc/design/Architecture.md`
§ The shape of the system states it; DR-0098 settled it and DR-0102 gave it
teeth before anything crosses it.

Nothing here is built yet. This directory exists so that the first consumer
of the seam, phase 2's VFS with its advisory-lock table and inotify registry,
has somewhere to put the interface and somewhere to put each realisation, and
so that `bin/check-substrate-line` can walk `core/` and refuse what belongs
here.

## Layout

`seam/seam.h` will be the interface: pid allocation and parent links, process
groups and sessions, wait queues and exit status, signal send to another
process, ttys and the line discipline, the `AF_UNIX` rendezvous namespace,
the SysV and POSIX IPC namespaces, advisory locks, the inotify watch registry,
process-shared futex queues, and `SIGKILL`, `SIGSTOP` and `SIGCONT` against a
process that may not be responding. Every operation names a Linux process by
pid and a Linux thread by tid, in the same way the substrate interface never
names an NT handle.

`seam/n/` will be the cross-process realisation: the supervisor `lk-init.exe`
of 0011 § 2 and § 5, reached over ALPC with a shared page and named sections.
`seam/h/` will be the in-process realisation: tables in `lk-kernel.exe`, and
the calls are calls.

The rule the checker enforces: no source under `core/` names an NT call, a
WHP call, an NT `CONTEXT` or `HANDLE`, a Win32 named-object or port call, or
dereferences a user pointer. The seam's realisations may name all of it, and
sit outside the walk for that reason.

## What is not settled

The operation list above is 0011's supervisor inventory restated, and phase 3
will find what it misses; `Architecture.md` § Not verified says so. Whether
the fast paths 0011 kept out of the supervisor (ring pipes in shared sections,
signal ports) are core mechanisms with one shape or seam operations with two
is decided when the first of them is written, not here.
