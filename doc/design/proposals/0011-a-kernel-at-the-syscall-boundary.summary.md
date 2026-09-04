# Proposal 0011, in two pages — a kernel at the syscall boundary

Summary of `0011-a-kernel-at-the-syscall-boundary.md`, status `draft`,
2026-09-04. The full proposal is the document to accept or refuse; this is what
it asks for, what it costs, and where it is still guessing.

## What is being asked

Stop emulating Linux at the C library's interface and emulate it at the
kernel's. A program built for el8 sees its kernel through one thing, the x86-64
syscall table, 330-odd numbered entries with six register arguments and a
negated errno on failure; everything else it thinks it sees (the auxiliary
vector, `/proc`, the layout of `struct stat`, the number `SIGUSR1` carries)
arrives through that table as bytes. Cygwin draws its boundary one layer up, at
`open`, `printf`, `pthread_mutex_lock`, keeping its own constants behind those
names. That choice is why the current design carries a veneer, five recorded
divergence kinds, translation tables, and a generator that renumbers errno and
then signals in Cygwin's source. At the syscall boundary none of that exists:
the constants are Linux's because the interface is; glibc is upstream code with
a `sysdeps` port of a few files; the certification bar becomes the Linux Test
Project and glibc's own test suite run here and on a real el8 kernel, with the
difference published as a table.

The price is that the kernel does everything itself. There is no `fhandler` to
inherit. There is a VFS to write, a process model, a signal path, a socket
layer. The proposal is the design of that thing, from first principles, with
Cygwin, WSL1, Interix, gVisor and `wepoll` mined for ideas and formats, never
for code.

## The shape

Four executables, one shared page. `lk-host.exe` is every Linux process: a PE
that imports only `ntdll`, never loads `kernel32`, never registers with
`csrss`, holding the kernel's state for that process as ordinary memory.
`lk-init.exe` is the supervisor, one per user, holding what no process can own
(pids, process groups, ptys and their line discipline, IPC namespaces, the lock
table) plus a handle to every live process, so that a crashed process gets real
exit cleanup and `SIGKILL`, `SIGSTOP` and `SIGCONT` always land. `lk-term.exe`
bridges a Windows console to a pty, so no Linux process ever touches a console
handle. `lk.exe` is the launcher Windows sees. The `lk-` prefix is a
placeholder.

Six decisions do most of the work.

`fork` is an NT executive clone (`NtCreateProcessEx` with a null section), the
primitive Interix used for fifteen years: copy-on-write at the memory manager,
section views shared, handles inherited; target under 2 ms, against Cygwin's
tens of milliseconds. `execve` replaces the image in place. The pid, the
handles and the supervisor connection survive, as they should.

The file system is a VFS over NTFS in WSL's on-disk format: `$LXUID`, `$LXGID`,
`$LXMOD`, `$LXDEV` as extended attributes; WSL's reparse tags for symlinks and
special files; `FileStatLxInformation` so that `stat` is one call without
opening the file, per-directory case sensitivity; POSIX delete and rename. A
tree written here reads identically from WSL, from Cygwin 3.6 and, for the
illegal-character escapes, from Explorer.

Root is a number the kernel owns. Because the permission check is the kernel's,
computed from EAs the kernel writes, uid 0 passes every mode check; setuid
binaries change the effective uid at `exec`; `sudo`, `chown`, `useradd` and
every rpm scriptlet work as written. Nothing about the Windows token changes.
This is the userland's expectation of a root, supplied; it is not a privilege.

Pipes are the kernel's own ring buffers in shared sections with NT events, so
`epoll` on a pipe is a wait rather than a poll; sockets are AFD reached
directly, so readiness is a completion; `AF_UNIX` is the kernel's, so
`SCM_RIGHTS`, `SO_PEERCRED`, datagram and seqpacket exist. `inotify` reports
`IN_CLOSE_WRITE` for Linux writers because the kernel sees every `close`.
`ptrace` is implementable because the kernel owns every thread. That is
`strace` and `gdb`, unported.

One core, two substrates. The core never assumes how user code runs. Under
substrate N (native) the userland is rebuilt with a toolchain that emits a gate
call where it would emit `syscall` and reads the thread pointer through `%gs`,
since spike 1 measured that a user-written FS base does not survive a
deschedule; that is the whole toolchain diff, three places. Under substrate H
(hypervisor, WHP) el8's shipped RPMs run unmodified on virtual processors with
a real `%fs` and a real `syscall`, at the cost of the hypervisor feature and a
VM exit per syscall. The proposal designs both. The operator chooses after the
measurements.

## What it reopens

DR-0000, the Cygwin re-faced floor, is replaced. DR-0005's bounded Linux claim
inverts: the ABI now is Linux's. DR-0004 and DR-0037, on licence, are reopened,
because a kernel written from specifications with no Cygwin code is not a
derivative of `winsup`; the recommendation is LGPL-2.1-or-later or a permissive
licence, operator's choice. Roughly forty records describing the veneer, faces,
wiring and low window describe a bootstrap the new design never passes through;
they retire together. The red-zone and delivery-shape records carry forward
unchanged; the spike transcripts remain measurements of this Windows.

## Where it is guessing

Every NT behaviour the design leans on is a reading, not a measurement: that
the executive clone works on Windows 11 24H2 with the operator's endpoint
product loaded; that an `ntdll`-only process survives that product's DLL
injection; that placeholders accept section views inside a terabyte-scale
reservation; that `FileStatLxInformation` returns the LX fields by name; that
AFD's poll request reports what `epoll` needs; that a WHP exit costs what
published numbers say. Phase 0 is six spikes, about two weeks. The proposal is
conditional on five of them; the sixth prices substrate H.

Ten open questions. Four are the operator's alone: the substrate, the language
(Rust `no_std` on `ntdll` recommended, C for the trampolines), the licence, and
the name.

## The order of work

Phase 0 measures. Phases 1 and 2 build a process and a file system; phase 3
brings the ported glibc, the supervisor, `fork`, `exec`, signals and a `bash`
that runs a script; 4 threads; 5 terminals; 6 readiness and sockets; 7 IPC and
locks; 8 tracing; 9 the LTP run and the package rebuild or install. Seventeen
verification criteria are written as commands or states, each at a named tier,
so that "done" is a transcript rather than a claim. If H is chosen it replaces
the gate, the arena and the toolchain half of phase 3. Nothing after.
