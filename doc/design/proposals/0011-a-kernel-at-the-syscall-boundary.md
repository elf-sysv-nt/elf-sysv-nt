# Proposal 0011 — a kernel at the syscall boundary: the Linux personality for NT, designed from first principles

Status: draft
Author: drafted 2026-09-04 for Philip Dye, from the operator's instruction to
start over
Date: 2026-09-04
Analysed against: `9780c09` on `main`; `6b7a25ed9` on the vendor tree's
`rhelcyg` (Cygwin 3.6.10 plus eight commits)
Classification: expensive to undo on every axis (the boundary the userland is
built against, the on-disk metadata format, the process model, the host
version floor) — full proposal. A decision record is owed, and so is a record
that names which of the standing decisions this reopens.

The operator asked for the best Linux kernel emulation that can be built on a
Windows 11 host without WSL: an emulation of the kernel Rocky Linux 8 runs on,
good enough for glibc 2.28 with the packages built on it, rivalling Cygwin in
what it lets a user do, free to take Cygwin's ideas and bound by none of its
decisions. Proposal 0010 was the previous attempt and it answered a narrower
question, which capabilities `elfsysv1.dll` could add to Cygwin's export table;
the operator's reading was that it designed from what Cygwin has rather than
from what the thing has to be. This proposal designs from the other end. It
asks what a Linux kernel is to the program above it, then what NT can supply
for each part, and only then what Cygwin, WSL, Interix, gVisor or anyone else
already learned about the same problem.

The governed set in `doc/design/` is not governing here. Where this proposal
contradicts a standing record it says so in section 6, so that acceptance can
reopen those records by name rather than by implication.

## Context and scope

A program built for el8 sees its kernel through exactly one interface: the
x86-64 system call ABI, 330-odd numbered entries reached by the `syscall`
instruction, taking up to six register arguments, returning a value or a
negated errno in `%rax`. Everything else a program might think it sees of the
kernel arrives through that interface as bytes: the auxiliary vector on the
initial stack, the vDSO mapped beside it, the contents of `/proc`, the layout
of a `struct stat`, the number `SIGUSR1` carries. glibc is not part of the
kernel; it is the first and largest program that speaks the ABI, the one whose
reading of it every other package inherits.

Cygwin emulates at a different boundary. It presents the C library's interface,
`open`, `printf`, `pthread_mutex_lock` as functions; behind those names it
keeps its own constants, its own structure layouts, newlib's bodies. That is
why the current design carries a veneer, five recorded divergence kinds,
translation tables in `veneer/xlat/`, plus a renumbering generator for errno,
then for signals: every constant the C library exposes has to agree with el8's,
which Cygwin's do not. Proposal 0010 sections 3 through 5 are the cost of that
boundary made explicit.

At the syscall boundary none of that exists. The constants are Linux's because
the interface is Linux's; a `struct stat` has el8's layout because the kernel
fills the buffer the caller hands it in the shape `<asm/stat.h>` gives it;
errno 22 is `EINVAL` because there is no second numbering anywhere in the
process. The C library above is glibc, unmodified in everything except how it
reaches the kernel and how it reaches its thread pointer; both of those are a
few files under `sysdeps/`. This proposal is the design of the thing below that
boundary.

In scope: the whole of it. The process model, the address space, threads,
signals, the file system, descriptors with their readiness, terminals, sockets,
IPC, credentials, time, `/proc`, tracing, the host-side components that hold
state no single process can own, the two ways user code can be run beneath the
boundary, and the order the work goes in. Out of scope: the rebuilt userland's
package-level acceptance, which is a project of its own whichever substrate
runs it, and every detail of NT that a spike has to measure before a design can
lean on it; those are named in section 8 rather than assumed.

## Goals and non-goals

Goals.

An unmodified glibc 2.28 source tree, with a `sysdeps` port small enough to
read in one sitting, builds and passes its own test suite against the kernel.
The Linux Test Project's syscall suite runs against the kernel, then against a
real el8 kernel; the difference is a published table rather than a feeling. A
Rocky 8 package that runs on el8 and needs nothing this proposal declines runs
here after a rebuild, with the same behaviour a differential trace can check.
Every capability Cygwin cannot supply because of where it drew its boundary —
`fork` in under two milliseconds, a real `epoll`, `ptrace` (so `strace`, so
`gdb`), a process that is root inside its own tree, `inotify`, `SCM_RIGHTS`, a
pty whose line discipline is the kernel's rather than the terminal's — is
supplied. A Windows program and a Linux program share a pipe, a file, a console
and a directory tree with no ceremony beyond the one Cygwin already asks for.
The kernel's on-disk formats are ones somebody else already maintains, so a
file tree written here is readable from WSL and from Explorer without a
converter.

Non-goals, each of which could reasonably have been a goal.

A security boundary. Nothing here confines a Linux process to less than the
Windows user can do; a process that is root inside the tree is still the
Windows user outside it. Sandboxing is a different design with a different
threat model, and WSL2's answer to it is a virtual machine.

Running el8's shipped binaries on the native substrate. Section 4.3 explains
why the thread pointer forces a rebuild there; the hypervisor substrate lifts
that limit, and the choice between them is open question 1.

An init system. The supervisor in section 4.2 is pid 1 in the sense that it
owns the process table and reaps orphans; it is not systemd, runs no units, and
`systemctl` reports that it is not booted with systemd, as it does on WSL1.

Network namespaces, cgroups, and the container stack above them. Mount, pid and
user namespaces fall out of the design nearly free and are kept; a network
namespace needs a second network stack, which NT does not offer a user-mode
process.

32-bit x86 binaries, and hosts other than x86-64. The design is
architecture-neutral above the substrate, and an aarch64 Windows host would
need a new substrate plus an aarch64 userland; neither is proposed.

Kernel modules, block devices, a `/dev/sda`. Files come from NTFS through the
VFS; there is no block layer to expose.

## The design

Subsections are cited as 4.N from the rest of this document, with the
metadata, context and goals above them as sections 1 to 3.

### 1. The boundary is the syscall, and the constants are Linux's by construction

The kernel presents the x86-64 Linux syscall table as it stood in kernel 4.18,
which is el8's, plus nothing. `uname` reports a 4.18 release string with a site
suffix, `/proc/sys/kernel/osrelease` agrees; every syscall number above what
4.18 defines returns `ENOSYS`, which is exactly what a 4.18 kernel does. glibc
2.28 probes `statx`, `copy_file_range`, `getrandom`, `memfd_create`,
`membarrier` at runtime, falling back when they are absent; here they are
present. It never probes `io_uring`, `rseq`, `clone3` or `openat2`, and those
are absent.

Every argument and every result has the layout the kernel's UAPI headers give
it: `struct stat` at 144 bytes with `st_ino` at offset 8, `sigset_t` at 8 bytes
on the kernel side of `rt_sigaction`, `struct sigaction` in the kernel shape
rather than glibc's, `struct epoll_event` packed to 12 bytes. The kernel has
one set of constants; they are these. There is no translation table because
there is nothing on the other side of one; where NT's constant differs (a
`FILE_ACTION_ADDED` becoming `IN_CREATE`) the mapping lives inside the body
that talks to NT, once, in the direction NT-to-Linux, and no Linux constant is
ever renumbered.

Two things follow that this project has been paying for elsewhere. The C
library is glibc's own code, so `printf`, `qsort`, `iconv`, the resolver, NSS,
locales, the dynamic loader are all upstream's; every divergence kind that
`doc/design/ABI-Boundary.md` records for the veneer is gone at once. And
certification has a reference implementation with a test suite: the kernel is
right when LTP and glibc's tests say the same thing here as on el8, which is a
stronger bar than any this project has been able to state.

The price is that everything the kernel does, it does itself. There is no
`fhandler_disk_file` to inherit; there is a VFS to write. Sections 4.4 through
3.14 are that work, and section 4.17 is what was borrowed to make it smaller.

### 2. The shape of the system

Four executables and one shared page. The `lk-` prefix is a placeholder for a
name the operator picks.

`lk-host.exe` is every Linux process. It is a PE image whose only import is
`ntdll.dll`; it never loads `kernel32`, never registers with `csrss`, so it is
the kind of process `smss` or `csrss` itself is. The kernel lives inside it as
ordinary code, and the ELF program lives above the kernel in the same address
space. On the native substrate the program calls the kernel through a gate
function; on the hypervisor substrate the program runs on a virtual processor
and the kernel handles its exits. Either way the kernel's state for this
process (its descriptor table, its VMA tree, its signal dispositions, its
credentials) is memory in this process, which is what lets `fork` be a clone of
the address space and nothing else.

`lk-init.exe` is the supervisor, one per Windows user per installation, started
by the first `lk-host` that finds none and kept alive by a job object. It holds
what no single process can own: the pid table with parent, group, session
links; the zombie list with the wait queues; the tty objects, the pty objects,
their line disciplines; the `AF_UNIX` rendezvous namespace; the SysV IPC
namespace, the POSIX one; the advisory lock table; the inotify watch registry;
a process handle for every `lk-host` alive. That last item is what makes the
supervisor a kernel rather than a daemon. When a process dies, however it dies,
the supervisor's wait on its handle completes, and the supervisor does the exit
cleanup a crashed process cannot do for itself: reparent its children, post
`SIGCHLD`, release its locks, decrement the writer count on its pipes, drop its
pty from the session. The supervisor is also the only Win32 process in the
system, so it is where anything needing `kernel32`, Winsock's helper DLLs or
the console API runs: creating a Windows child on behalf of an `execve`,
answering a netlink dump from the IP helper API, seeding the per-process random
pool from `BCryptGenRandom`.

`lk-term.exe` is the terminal bridge. When a Linux program is started from a
Windows console, the bridge owns the console handle, allocates a pty from the
supervisor, puts the console into VT mode, pumps bytes between the master side
and the console; window resizes become `TIOCSWINSZ` on the master; Ctrl-C
arrives as byte `0x03`, which the line discipline turns into `SIGINT` to the
foreground process group exactly as it would on Linux. No Linux process ever
holds a Windows console handle. That one rule removes the whole class of
console-emulation defects Cygwin's `fhandler_console` exists to manage; it
makes `isatty`, job control, `less`, `vim` and `tmux` behave because they are
talking to a real pty.

`lk.exe` is the launcher, and it is what Windows sees. A copy or a hard link
named `bash.exe` on a Windows `PATH` asks the supervisor to exec `/usr/bin/bash`
with its arguments, hands over its standard handles, waits, and exits with the
child's status; when those handles are a console, it is also the bridge. From
inside the system no launcher is involved: a Linux process execs another by
replacing its own image.

The shared page is a section the supervisor writes and every process maps
read-only: boot id, boot time, hostname, the mount table, a snapshot of the pid
table for `/proc` or `kill -0`, and the small tables sections 4.8 and 4.9 need
for cheap cross-process checks. It is this system's `KUSER_SHARED_DATA`, and it
is read without a message the way that page is.

    Windows console ──┐
                      │ VT bytes
              ┌───────┴────────┐  ALPC  ┌──────────────┐
              │  lk-term.exe   │───────►│ lk-init.exe  │ pid table, ptys, ldisc,
              │  (Win32)       │        │ (Win32, one  │ IPC namespaces, locks,
              └───────┬────────┘        │  per user)   │ handle to every process
                      │ pty master      └──────┬───────┘
                      ▼                        │ ALPC + process handles
              ┌────────────────┐               │
              │  lk-host.exe   │◄──────────────┘
              │  (ntdll only)  │
              │ ┌────────────┐ │
              │ │ ELF program│ │   ← glibc 2.28, Linux sysdeps, syscall table
              │ ├────────────┤ │
              │ │  kernel    │ │   ← VFS, mm, signals, fd table, threads
              │ ├────────────┤ │
              │ │ substrate  │ │   ← N: gate + NT threads, or H: WHP vCPUs
              │ └────────────┘ │
              └────────────────┘
                      │ NtCreateFile, NtMapViewOfSection, AFD, ALPC ...
                      ▼
                    ntdll / NT executive

### 3. The core and the two substrates

Everything from the syscall table down to the point where user code has to be
run is the core, and the core is written once. What differs is the substrate:
how user code executes, how it enters the kernel, how its address space is
realised, how a thread is interrupted from outside. gVisor calls this a
platform and keeps three of them behind one sentry; the interface here is the
same shape, about the same size.

    as_map(vma, backing, offset, prot)     realise a VMA, or part of one
    as_unmap(range)                        drop it
    as_protect(range, prot)                change protection at page granularity
    as_clone() -> child                    the fork primitive
    thread_start(tid, ctx, tls)            run user code from a register state
    thread_interrupt(tid)                  force it into the kernel, soon
    thread_context(tid) / set_context      read or replace its user registers
    tp_set(tid, base)                      set the thread pointer
    user_copy_in / user_copy_out           the only way the core touches user memory

Substrate N, native, runs user code as ordinary instructions on ordinary NT
threads in the host process. The syscall instruction is not available to it,
because a `syscall` executed on NT enters the NT executive with a Linux number
in `%rax`, where no user-mode handler can intervene; the thread pointer is not
available to it, because `spike/fs-base-persistence/` measured on 2026-08-29
that a user-written FS base does not survive a deschedule. So the userland is
rebuilt against a toolchain that emits a call to the gate where it would have
emitted `syscall`, and reaches the thread pointer through `%gs` at a TEB word
the kernel owns. Those two changes are the entire difference between this
target and `x86_64-pc-linux-gnu`; section 4.16 sizes them.

The gate is a function with the syscall register convention: number in `%rax`,
arguments in `%rdi %rsi %rdx %r10 %r8 %r9`, result in `%rax`, `%rcx` and `%r11`
clobbered so that glibc's existing inline asm constraints stay correct. It
switches to a per-thread kernel stack before it does anything else, for the
reason a real kernel does: a thread with a 16 KB user stack must not run
`NtCreateFile`'s frames on it, and the signal-delivery code must not have to
reason about the red zone of a stack it is about to write to. Its address is
published in the auxiliary vector as `AT_SYSINFO`, which i386 glibc already
knows how to read and call, and in the vDSO as `__lk_syscall`, so a static
binary and a dynamic one find it the same way. A glibc built for this target
has `INTERNAL_SYSCALL` expand to `call *_dl_sysinfo(%rip)`.

Substrate H, hypervisor, runs user code on a Windows Hypervisor Platform
partition owned by the host process: one virtual processor per Linux thread,
each driven by one NT thread that loops on `WHvRunVirtualProcessor`. Guest
physical memory is the host process's own memory, mapped by `WHvMapGpaRange`,
so the kernel reads and writes user memory at the same addresses the guest
uses. A small ring-0 shim in the guest, a few hundred instructions, owns the
IDT and the syscall MSRs and does one thing on every syscall or exception:
spill the registers to a per-vCPU page and exit. The core sees the same
`thread_interrupt`, the same `as_map`, the same register state as under N. What
H buys is everything N cannot have: a real FS base, so el8's shipped binaries
run unmodified; a real `syscall` instruction, so `syscall(2)` callers and
inline-asm callers work; page tables the kernel writes itself, so `mmap` is
page-precise, `fork` is copy-on-write at page granularity, `mprotect` never
meets a 64 KB granule. What it costs is a VM exit per syscall, another per
first-touch fault, five to fifteen microseconds on WHP against under one for a
gate call; the "Windows Hypervisor Platform" optional feature; a ring-0 shim
that has to be right.

Both substrates fit under one core because the core never assumes how a VMA is
realised, nor how a thread was entered. Section 4.4 is written for N, whose
constraints are the harder ones, and marks where H is simpler.

### 4. The address space

The kernel keeps the address space of record itself: a tree of VMAs keyed by
range, each carrying its backing (anonymous, a file at an offset, a shared
section, the vDSO, the stack), its protection, its flags, and its name for
`/proc/self/maps`. Every `mmap`, `munmap`, `mprotect`, `brk`, `mremap`,
`madvise` edits that tree first, then asks the substrate to realise the edit
second. That is the ordering Linux uses; it is what lets `/proc/self/maps` be
exact; it is what lets `fork` reproduce the child's mappings from a description
rather than from a scan.

Under N the substrate realises VMAs onto NT virtual memory, and NT has two
rules the design has to absorb. The first is that a reservation's base and a
section view's base are 64 KB aligned, while a page is 4 KB. The second is that
a reservation is freed whole, never in part. The answer to both is an arena: at
exec, the substrate reserves the entire user range it intends to hand out
(everything from 64 KB up to the top of the 128 TB space that NT has not
already taken for `ntdll`, the PEB and the TEBs) as placeholders in the sense
`NtAllocateVirtualMemoryEx` gives the word. A placeholder can be split at 64
KB, then replaced by a section view or a committed range, without ever being
released; the kernel can put a file view at any 64 KB boundary, anonymous
memory at any page boundary; `munmap` becomes a decommit that leaves the
placeholder standing. NT's VADs are then the mechanism and the kernel's tree is
the truth.

Anonymous memory commits lazily. A `mmap` of 64 GB with `MAP_NORESERVE`, the
shape a garbage collector or a sanitizer asks for, must not charge 64 GB
against the commit limit; so an anonymous VMA is a placeholder until it is
touched; a vectored exception handler catches the first-touch fault, commits
the 64 KB chunk around it, resumes. The same handler is where `SIGSEGV` comes
from when the address is not in any VMA, where `SIGBUS` comes from when a file
view is touched past its file's end; the two are one mechanism with a three-way
branch. Small mappings, under a threshold the implementation tunes, commit
eagerly because a fault costs more than a commit.

File mappings are section views where NT permits and copies where it does
not. `MAP_PRIVATE` of a file at an address whose distance from the file offset
is a multiple of 64 KB is a copy-on-write view, sharing pages with every other
process mapping the same file, which is what makes fifty processes' `libc.so`
cost one copy of its text. el8's linker lays out shared objects with
`p_vaddr ≡ p_offset (mod 2 MB)`, so every `PT_LOAD` a real el8 library carries
satisfies the rule and the loader never hits the fallback. Where the rule
fails, a `MAP_PRIVATE` mapping is read into anonymous memory instead, which
POSIX permits, since it never promised that a private mapping observes later
writes to the file; a `MAP_SHARED` mapping that fails the rule returns
`EINVAL`, which is what a Linux filesystem lacking `mmap` support returns,
and is recorded. The tail of a segment past the file's last page is anonymous
and copied, as Linux's own `elf_map` does with its partial page.

Executable mappings are file-backed views with `PAGE_EXECUTE_READ`, obtained by
opening the ELF file with `FILE_EXECUTE` and creating the section with execute
in its maximum protection. That is a deliberate choice against anonymous
executable memory: the loader's mappings of `ld.so`, of the program, of each
library are views of the files on disk, which is what an endpoint product
expects an image mapping to be; no anonymous region becomes executable unless a
JIT asks for one with `mprotect`.

`brk` is a reserved region placed directly after the program's `bss`, grown by
committing. `mremap` with `MREMAP_MAYMOVE` allocates, copies, unmaps, since NT
cannot move committed pages; without the flag it grows in place when the next
placeholder is free. `madvise(MADV_DONTNEED)` decommits, which gives zero-fill
on next touch, the semantics Linux gives anonymous memory. Huge pages are
declined in `/sys/kernel/mm/transparent_hugepage/enabled` as `never`.

Under H every one of those paragraphs is simpler. The kernel writes the guest
page tables, so a VMA is realised at page granularity by writing entries, a
file mapping at any alignment is a matter of which host pages back which guest
pages, `fork` marks every writable entry read-only in parent and child and
copies on the first write fault; there is no arena because there is no NT VAD
in the guest's way. The host memory behind guest physical pages is still NT
memory in the host process, so the lazy-commit handler still exists, one level
down.

### 5. Processes: fork by cloning, exec in place, a supervisor for what crosses

`fork` is `NtCreateProcessEx` with a null section handle, which asks the NT
executive to create a process whose address space is a copy-on-write clone of
the caller's, and then one thread in it started at the caller's return address
with the caller's registers. That primitive is how the POSIX subsystem in
Windows NT 3.1 through Interix implemented `fork` for fifteen years, it is what
`RtlCloneUserProcess` in today's `ntdll` wraps, and it is what Windows Error
Reporting uses when it snapshots a process. Everything the kernel holds for the
process is memory in the process, so the child has the parent's descriptor
table, VMA tree, signal state, credentials at once, with no copying by anything
but the memory manager. Section views are cloned as views of the same section,
which is why a `MAP_SHARED` region is shared with the child without any
handling. Handles marked inheritable are duplicated into the child; the kernel
marks every handle that backs a descriptor inheritable so that the descriptor
table it just inherited refers to handles that exist.

What the child does not have, it rebuilds in the first microseconds. NT gives
the cloned thread a fresh TEB, so the fork wrapper, which is kernel code that
touches no thread-local state, writes the thread pointer, the stack guard and
the pointer guard into the new TEB's slots before anything else runs. The child
then creates its signal thread, opens its own ALPC connection to the
supervisor, announces its pid, and returns 0. Other threads of the parent do
not exist in the child, as on Linux. Because the kernel's own data is in the
copied memory, `fork` waits until no other thread is inside the kernel before
it clones, a barrier every syscall entry takes as a reader and `fork` takes as
a writer, so that no lock in the copied heap is held by a thread the copy does
not contain. Cygwin's `fork` in `winsup/cygwin/fork.cc` creates a suspended
process from the image with `CreateProcessW` (line 366 at `6b7a25ed9`), then
copies data, bss, heap, stack and every mapping it knows about into it, which
is why it takes tens of milliseconds and fails when an address is not free in
the child; none of that machinery exists here, and section 4.17 records it as
the one Cygwin idea rejected on purpose.

`execve` replaces the image in place. The kernel tears down every VMA that is
not its own, terminates every thread but the caller, closes every descriptor
marked `CLOEXEC`, resets signal dispositions that had handlers, then runs the
binary format handler on the new file from a kernel stack. For an ELF file the
handler is `binfmt_elf` as Linux has it: map the program's segments, map the
interpreter named by `PT_INTERP` if there is one, build the initial stack with
`argc`, `argv`, `envp`, the auxiliary vector, the random bytes `AT_RANDOM`
points at; map the vDSO; jump to the interpreter's entry. The kernel is not a
dynamic loader and has none; `ld.so` is glibc's, unmodified, doing its work
through `mmap`, `mprotect`, `open` and `read` the way it does on Linux. Nothing
about the NT process changes across an exec, so the pid, the supervisor
connection, the process handle the supervisor holds, and the console or pipe
handles the launcher gave it all persist, which is what `exec` means. An
`execve` of a file that begins `MZ` is the PE handler; that one does go through
the supervisor: it runs `CreateProcess` in the supervisor on the process's
behalf with the argument vector quoted to Windows's rules, the working
directory translated, the environment converted, the standard descriptors
turned into NT handles a Win32 process can use, a pump thread standing in for
any descriptor that is a kernel object rather than an NT file. The Linux side
sees an ordinary child with a pid, a wait status, and a `SIGKILL` that
terminates it.

Pids are the supervisor's. A process gets its pid at creation over ALPC, thread
ids from the same space in blocks of sixteen so that `clone` does not
round-trip for each. The supervisor keeps the parent link, the process group,
the session, the controlling terminal, and the exit status once there is one;
`wait4` and `waitid` are ALPC calls that block on an event the supervisor
signals; `SIGCHLD` is raised by the supervisor into the parent. `getppid`,
`getpgid`, `getsid`, `kill -0` read the shared page. `setsid`, `setpgid` and
the job-control ioctls are messages, because the pty's line discipline that
enforces them lives in the supervisor too. A process that exits normally
reports its status and cleans its own cross-process state; a process that is
terminated, by `SIGKILL` or by anything else, is cleaned by the supervisor from
its process-info section, a small region each process publishes at start that
lists the cross-process objects it holds. `SIGKILL` is `NtTerminateProcess` on
the supervisor's handle; `SIGSTOP` is `NtSuspendProcess`, `SIGCONT`
`NtResumeProcess`, both done by the supervisor, so a stopped process stays
stoppable and killable whatever state its own threads are in.

The launcher is the same process shape from outside. `lk.exe` connects to the
supervisor, which creates an `lk-host` with `NtCreateUserProcess`, passes it
the launcher's standard handles by duplication, and gives the launcher a handle
to wait on. A Windows program that runs `grep.exe` sees one Win32 process that
exits with `grep`'s status; a Windows Ctrl-C reaching the launcher becomes
`SIGINT` to the process, or to its foreground group when a pty is in the way.

### 6. Threads, the thread pointer, and futexes

`clone` with the flag set NPTL uses (`CLONE_VM | CLONE_FS | CLONE_FILES |
CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID`) creates an NT thread with
`NtCreateThreadEx`, records the tid, the NT handle, the `child_tid` address and
the robust-list head in the kernel's thread table, then starts it in a
trampoline. NT allocates the thread its own stack, which the trampoline keeps
as the thread's kernel stack and never returns to; it writes the TLS base the
caller supplied into the thread-pointer slot, stores the tid where `parent_tid`
points, switches `%rsp` to the stack the caller supplied, and enters user code
at the caller's `%rip` with the caller's registers. On exit the thread walks
its robust list, zeroes `*child_tid`, wakes any futex on it, and terminates.
`clone` with `CLONE_VFORK` but without `CLONE_VM` is the `vfork` glibc's
`posix_spawn` uses, implemented as `fork` plus a wait on the child's `exec` or
exit; `clone` with any other combination is `EINVAL`.

The thread pointer under N is a word at a fixed offset in the TEB, reached
through `%gs`. `spike/gs-thread-pointer/` measured four carriers on 2026-08-29,
and found three that persist and address at five cycles per access; the hazard
it recorded for a fixed `TlsSlots` index, that a DLL somebody injects might
`TlsAlloc` the same slot, is smaller in a process that loads nothing but
`ntdll`; the operator's standing choice of a word below the stack base works as
well. Either way the ABI is: `%gs:TP` holds the TCB pointer, `%gs:TP+8` the
stack-protector canary, `%gs:TP+16` the pointer guard, and these three are what
glibc's `tls.h`, `-fstack-protector`, `PTR_MANGLE` read. Setting them is a
store, not a syscall; `arch_prctl` exists and returns `EINVAL` for
`ARCH_SET_FS`, since a program on this substrate never asks. Under H,
`arch_prctl(ARCH_SET_FS)` writes the vCPU's FS base and the three words are
where glibc always kept them.

`futex` is `RtlWaitOnAddress`, `RtlWakeAddressSingle`, `RtlWakeAddressAll`,
which `ntdll` exports directly. `FUTEX_WAIT` and `FUTEX_WAIT_BITSET` with a
timeout, absolute or relative, map one to one: wait while the word still holds
the expected value, wake when told. `FUTEX_WAKE` of one waiter is
`WakeAddressSingle`; of `n` waiters where `1 < n < INT_MAX`, it is
`WakeAddressAll`, which wakes more than asked, a spurious wakeup the futex
contract allows and glibc's loops absorb, recorded as a fairness delta rather
than a correctness one. `FUTEX_REQUEUE` and `FUTEX_CMP_REQUEUE` wake rather
than requeue, with the same recording. The PI operations take and release the
lock word with the non-PI protocol and grant no priority inheritance, since NT
offers none to a user-mode waiter; `PTHREAD_PRIO_INHERIT` mutexes therefore
work and do not boost. Process-shared futexes, a word in `MAP_SHARED` memory
waited on from two processes, are the one case `WaitOnAddress` cannot serve,
since its wait queue is per process; they fall back to a keyed wait through the
supervisor, slower and correct. Robust lists are walked at thread exit by the
kernel exactly as Linux walks them, so `EOWNERDEAD` works for a thread that
dies; for a process that dies with a robust mutex in shared memory, the
supervisor cannot walk memory that no longer exists, which is recorded.

`set_tid_address`, `gettid`, `tgkill`, `sched_setaffinity` on a tid,
`sched_getcpu` and `/proc/self/task/` all read the thread table. Thread names
from `prctl(PR_SET_NAME)` are kept there and shown in `/proc`.

### 7. Signals

A signal is delivered at the moments a kernel delivers one: on the way back to
user mode from a syscall, when a blocking wait is interrupted, when a fault in
user code is turned into a signal, and, for a thread running user code with
nothing pending, by interrupting it. The first three need no machinery beyond a
check at the gate's exit path, an alertable wait, and the vectored exception
handler that already exists for lazy commit. The fourth is the substrate's
`thread_interrupt`: under N, `NtSuspendThread`, `NtGetContextThread`, a check
that the thread is in user code rather than inside the kernel (a per-thread
flag the gate sets on entry and clears on exit, the analogue of the kernel/user
distinction a real kernel gets from the CPU), then a rewrite of the context so
that the thread resumes in the signal trampoline with the frame built, then
`NtResumeThread`; if the thread is inside the kernel the signal is left pending
for the gate's exit path to deliver. That is the mechanism Cygwin's
`sigdelayed` in `winsup/cygwin/exceptions.cc` uses; it is what Go's runtime and
every garbage collector on Windows use to stop a thread; the difference from
Cygwin is that the in-kernel check makes it safe to apply to a thread that
might hold a lock. Under H, `thread_interrupt` is
`WHvCancelRunVirtualProcessor`; the vCPU's exit is the moment.

The frame is Linux's `rt_sigframe`, built 128 bytes below `%rsp` to respect the
red zone or on the alternate stack when `SA_ONSTACK` asks, with `ucontext`,
`siginfo`, the saved FPU state and a return address that points at glibc's own
`__restore_rt`, which calls `rt_sigreturn`. `rt_sigreturn` reads the frame
back, verifies it, restores the registers, and resumes; nothing about the
frame's shape is this project's, which is why `gdb`'s unwinder recognises it,
why `siglongjmp` out of a handler works. `siginfo` carries what Linux puts
there: `si_pid` and `si_uid` for a `kill`; `si_addr` and a `SEGV_MAPERR` or
`SEGV_ACCERR` decided from the VMA tree for a fault; `si_code` `CLD_EXITED` or
its siblings for `SIGCHLD`; the `si_value` a `sigqueue` sent.

Signals between processes go through a per-process signal port with a queue: a
section holding a bounded queue of `siginfo`, plus an NT event, both named from
the pid under the installation's prefix, both ACLed to the user. A sender maps
the target's queue, appends, and sets the event; the target's signal thread
wakes, consults the thread table for a thread not blocking the signal, then
either interrupts it or marks it pending. `kill` never involves the supervisor
except for `SIGKILL`, `SIGSTOP` and `SIGCONT`, which the supervisor applies to
the process handle because a target that has stopped responding must still
honour them. Real-time signals queue in order with their values; standard
signals coalesce as on Linux. `sigaltstack`, `SA_RESTART` against the list of
restartable syscalls, `SA_NODEFER`, `SA_RESETHAND`, `SA_NOCLDWAIT`, `signalfd`,
`sigtimedwait`, `pause` and `ppoll`'s atomic mask swap are all kernel-side, all
Linux's semantics, since there is no second signal implementation underneath to
disagree with.

### 8. Files: a VFS over NTFS in WSL's on-disk format

The VFS is the largest piece and the one where the borrowed formats pay most.
Path resolution, permissions, the mount table, symlinks, special files,
`getdents64`, timestamps and inode numbers are the kernel's; NTFS supplies the
storage; the metadata Linux needs that NTFS does not natively carry is kept in
the extended attributes plus reparse tags that Microsoft defined for WSL1's
DrvFs, which WSL2 still honours: `$LXUID`, `$LXGID`, `$LXMOD`, `$LXDEV` as EAs;
`IO_REPARSE_TAG_LX_SYMLINK` (`0xA000001D`) with a UTF-8 target for symlinks;
`IO_REPARSE_TAG_LX_FIFO`, `LX_CHR`, `LX_BLK` for special files;
`IO_REPARSE_TAG_AF_UNIX` for socket files. Cygwin 3.6 already writes the
symlink form (`symlink_wsl` at `winsup/cygwin/path.cc:1976`); it already
escapes the nine characters NTFS refuses in a name into the private range
`U+F000` to `U+F0FF` (`path.cc:515`), the same escape WSL uses; both are
adopted so that a tree written here reads identically from WSL, from Cygwin,
and, for the escapes, from Explorer. The root file system is a directory the
installer marks case-sensitive with `FILE_CASE_SENSITIVE_INFORMATION`, per
directory, as WSL does, so `Makefile` and `makefile` coexist under `/`; mounts
of Windows drives at `/mnt/c` stay case-insensitive, synthesising modes the way
DrvFs does when the EAs are absent.

`stat` is one call. `NtQueryInformationByName` with `FileStatLxInformation`
returns size, times, link count, file id, the four LX attributes, in a single
request without opening the file; Microsoft added that class for WSL, which is
why a `git status` over a large tree can hope to run at a useful fraction of
Linux speed here where Cygwin, which opens each file to read its ACL, cannot.
`st_ino` is the NTFS file reference number, `st_dev` the volume serial, both
stable across opens; `st_nlink` is NTFS's link count; `st_ctime` is NTFS's
change time; `statx` gets a birth time.

Path resolution takes the fast path first and walks only when it must. The
whole path, translated to an NT path under the mount's root, goes to
`NtCreateFile` in one call; NTFS resolves its own junctions and Windows
symlinks in the kernel, returning `STATUS_IO_REPARSE_TAG_NOT_HANDLED` when it
meets an LX symlink, at which point the VFS reads the tag, splices the target
in, and continues component by component from there, with the Linux rules for
`..` across a symlink, for `ELOOP` at 40, for permission on each directory, for
a trailing slash. Relative opens use the directory handle the descriptor
already holds as `RootDirectory`, which is what makes `openat` and its siblings
cheap and race-free in the way Linux means them to be. A per-process cache of
directory handles keyed by path keeps repeated resolution of the same prefix
from re-opening it; it is invalidated by the process's own renames or unlinks,
bounded by a short lifetime against other writers.

Deletion uses NTFS's POSIX semantics, `FILE_DISPOSITION_POSIX_SEMANTICS`;
rename uses `FILE_RENAME_POSIX_SEMANTICS`; Cygwin already reaches for both
(`syscalls.cc:768`, `:2740`). An open file can be unlinked and its name reused;
a rename replaces an open target. Both need NTFS on Windows 10 1709 or later;
on other file systems the VFS falls back to the delete-on-close and
rename-with-retry sequence Cygwin uses, and records the difference. Every open
uses all three share modes, so a Linux program never sees a sharing violation
from another Linux program; a Windows program holding a file without
share-delete still makes `unlink` fail, and that is reported as `EBUSY` rather
than mapped to something Linux would say.

Permissions are Linux's discretionary check, computed by the kernel from the
mode in `$LXMOD` and the process's credentials, before NT's own ACL check,
which the Windows user passes on their own files. A file without the EA gets a
synthesised mode: `0755` for directories and for files whose name ends in a few
executable extensions, `0644` otherwise, owner the calling process's uid,
tunable per mount. `chmod`, `chown`, `mknod` and the `setuid` bits write the
EAs; `umask` is honoured at creation. Ownership therefore means what the tree
says it means, and section 4.13 draws the consequence.

Directory reads are `NtQueryDirectoryFile` with
`FileIdBothDirectoryInformation`, which yields name, file id and attributes in
one pass, enough for `d_type` and `st_ino` without a second call per entry.
Hard links are `FileLinkInformation`. Timestamps are set with
`FileBasicInformation` at 100 ns resolution; `utimensat`'s `UTIME_OMIT` and
`UTIME_NOW` are honoured. Extended attributes a program sets with `setxattr` in
the `user.` namespace are stored as NTFS EAs prefixed to avoid the LX names;
`security.` and `system.` return `ENOTSUP`. `flock` and `fcntl` byte-range
locks are advisory, which NT's own byte-range locks are not, so the lock table
is the supervisor's, keyed by device and inode, with `fcntl`'s per-process
release-on-any-close rule kept beside `flock`'s per-open-file-description rule;
NT locks are never taken, so a Windows program is unaffected and unprotected,
as it would be by a Linux advisory lock over NFS.

`inotify` watches are `NtNotifyChangeDirectoryFile` requests, one per watched
directory, completed onto a per-process completion port and translated to
`IN_CREATE`, `IN_DELETE`, `IN_MODIFY`, `IN_ATTRIB`, `IN_MOVED_FROM` and
`IN_MOVED_TO` with the rename pair sharing a cookie. Those events cover Windows
writers as well as Linux ones. The events NT cannot see, `IN_OPEN`,
`IN_ACCESS`, `IN_CLOSE_WRITE` and `IN_CLOSE_NOWRITE`, are generated by the
kernel in the process that opens or closes, which is a thing a kernel can do
that a change-notification consumer cannot: the supervisor keeps a set of
watched directory inodes with those mask bits on the shared page; `close`
probes it with one hash lookup; on a hit it posts the event to the supervisor,
which fans it out. A build tool waiting for `IN_CLOSE_WRITE` therefore works
for files written by Linux processes, seeing `IN_MODIFY` alone for files
written by Windows ones; the delta is recorded, narrower than any alternative
offers.

`/proc`, `/sys`, `/dev` and `/dev/pts` are in-kernel file systems mounted at
boot; `/dev/shm`, `/run`, `/tmp` are ordinary directories under the root, which
makes `shm_open` a plain file and `mmap` of it a shared section with no special
case. The mount table is per process, cloned on `fork`, so
`unshare(CLONE_NEWNS)` costs a copy of a small array; `mount` of a bind, or of
a Windows path, at a new point is an edit to it; `mount` of a block device is
`ENODEV`.

### 9. Descriptors, pipes and readiness

A descriptor is an index into a per-process table of references to open file
descriptions, with the Linux rules for numbering (lowest free), `dup` sharing
the description and therefore the offset, `O_CLOEXEC` on the descriptor,
`RLIMIT_NOFILE` on the count. An open file description is a kernel object with
a small set of operations that every kind implements: `read`, `write`, `readv`,
`pread`, `ioctl`, `poll_arm`, `poll_check`, `mmap`, `fstat`, `close`,
`fork_fixup`. That is Cygwin's `fhandler` hierarchy with the names changed, and
it is borrowed on purpose, since the shape is right. The kinds are regular
files, directories, pipes, terminals, sockets of three families, `eventfd`,
`timerfd`, `signalfd`, `epoll`, `inotify`, `memfd`, the `/proc` and `/dev`
synthetics, and one more that has no Linux counterpart: the foreign handle, an
NT file, pipe or console the process inherited from a Windows parent, which
behaves as a file or a pipe as best it can, and it is the one place in the
system where `poll` polls.

A pipe between Linux processes is not an NT pipe. It is a ring buffer in a
shared section with two NT events, data-available and space-available, and a
header holding reader and writer counts; `read` and `write` are memory copies
under a lock word, `poll` is the state of an event, `O_NONBLOCK` is a check of
the counts, `PIPE_BUF` atomicity is the ring's, and `fork` shares it because
section views clone. End-of-file and `EPIPE` come from the counts, which a
process decrements at `close` and the supervisor decrements for it at death
from the process-info section. What this buys is a readiness signal NT never
gave its own pipes: `epoll` on a pipe is a wait on an event, level-triggered by
reading the counts, edge-triggered by a generation number in the header; a
shell pipeline of six stages costs no polling thread. Cygwin's `select.cc`
polls pipes from a thread because it took NT's pipe as the transport; 0010
accepted that as the cost of `epoll`; taking the transport into the kernel
removes the cost.

`epoll` is a kernel object holding an interest list keyed by open file
description, plus a ready list, as Linux's is. `epoll_wait` gathers the NT
waitables behind the interests (events for pipes, `eventfd`, `signalfd`; AFD
poll completions for sockets; timer objects for `timerfd`; the completion port
for `inotify`) into one `NtWaitForMultipleObjects`, fans out past 64 through
helper threads the way Cygwin's `select_stuff` does, then re-checks each ready
interest's `poll_check` before reporting it, which is what makes level
triggering exact, `EPOLLONESHOT` and `EPOLLEXCLUSIVE` honest. `poll`, `ppoll`,
`select` and `pselect` are the same engine with a temporary interest list.
`eventfd` is a counter under an event with `EFD_SEMAPHORE`; `timerfd` is a
waitable timer created with the high-resolution flag so that a
`timerfd_settime` of 100 µs fires in 100 µs rather than at the next 15.6 ms
tick; `signalfd` reads the process's own queue.

The foreign handle is where Windows interop lives and where its cost is paid.
Standard input from `cmd.exe`'s pipe, a file handle from a Windows parent,
the output side of `dir | grep`: each becomes a descriptor whose readiness is
found by peeking (`FSCTL_PIPE_PEEK`, `NtQueryInformationFile`) from a poller
thread on a short interval, exactly Cygwin's `peek_pipe`. A program that
`select`s on such a descriptor wakes late by up to the interval; a program
that reads it does not. This is the whole of the pipe-polling problem, fenced
to the one descriptor kind that talks to a process this kernel does not
govern.

### 10. Terminals

A tty is a kernel object with a line discipline, and both live in the
supervisor, because a pty has two ends in different processes and the
foreground process group it enforces belongs to a session the supervisor
already owns. `posix_openpt` asks the supervisor for a new pair; the master
descriptor in the caller and the slave descriptor in whoever opens `/dev/pts/N`
are each a pair of the ring pipes from section 4.9, one in each direction, plus
an ALPC handle for the ioctls. The line discipline runs on the supervisor's
side of the ring: canonical mode with its editing characters; echo; `ICRNL` and
`ONLCR`; `ISIG` turning `^C`, `^Z`, `^\` into `SIGINT`, `SIGTSTP`, `SIGQUIT`
for the foreground group; `TOSTOP`; `VMIN` and `VTIME` in raw mode;
`TIOCSWINSZ` raising `SIGWINCH`; `TIOCSCTTY` and `TIOCSPGRP`; `TCSETSW`
draining; `TCFLSH`; the `termios` structure at its Linux size with `NCCS` 32.
It is a transliteration of `drivers/tty/n_tty.c`'s behaviour from the man pages
rather than of its code, since that file is GPL and the behaviour is specified.

The bridge in section 4.2 is a client of this and nothing more. When a Linux
program's standard descriptors are a Windows console, the launcher-as-bridge
makes a pty, becomes the session leader's parent, and translates: console input
events in VT mode to bytes on the master, master output to the console with
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, `WINDOW_BUFFER_SIZE_EVENT` to
`TIOCSWINSZ`. Windows Terminal, ConHost and any ConPTY host see a process that
writes VT sequences, which is what they are built for. The reverse direction, a
Windows console program started by a Linux process whose terminal is a pty,
runs under a `CreatePseudoConsole` the supervisor makes and pumps into the pty,
so `cmd.exe` and `powershell.exe` from a Linux shell get a working console
rather than a pipe. `/dev/tty`, `/dev/console`, the controlling terminal rules
are the kernel's.

### 11. Sockets

Internet sockets are AFD, the `\Device\Afd` driver beneath Winsock, reached
with `NtDeviceIoControlFile` over the `IOCTL_AFD_*` request set: bind, connect,
listen, accept, send, receive, `sendto`, `recvfrom`, `getsockopt`,
`setsockopt`, shutdown, and `IOCTL_AFD_POLL`, which is a genuine readiness
query and is how `wepoll` implements `epoll` on Windows. AFD is undocumented
and has been stable since NT 4; ReactOS carries a complete public description,
`wepoll`'s use of the poll request is BSD-licensed, and the host process cannot
load `ws2_32.dll` in any case since it has no `kernel32`. What that buys is a
socket whose readiness is an NT completion rather than a Winsock `select`, so
`epoll` on ten thousand sockets is one completion port, whose flags map without
a shim: `MSG_PEEK`, `MSG_DONTWAIT`, `MSG_NOSIGNAL`, `SO_REUSEADDR` in its Linux
sense (bind while the previous owner is in `TIME_WAIT`, which is NT's default,
with `SO_EXCLUSIVEADDRUSE` set so that NT's stronger sense is not accidentally
granted), `SO_KEEPALIVE`, `TCP_NODELAY`, `IPV6_V6ONLY`, and the `SIOCGIF*`
ioctls answered from a table the supervisor refreshes. `AF_INET6` dual-stack,
`SOCK_DGRAM`, `SOCK_RAW` where the Windows user is allowed one, `IPPROTO_ICMP`
datagram sockets (Linux's unprivileged `ping`) via the supervisor's
`IcmpSendEcho` are in; `AF_PACKET` is `EAFNOSUPPORT`.

`AF_UNIX` is the kernel's own, not Windows's `afunix.sys`, because the parts
Linux programs depend on are the ones `afunix.sys` lacks: `SCM_RIGHTS`,
`SO_PEERCRED`, `SOCK_DGRAM`, `SOCK_SEQPACKET`, the abstract namespace and
credentials passing. `bind` on a path creates a socket file bearing
`IO_REPARSE_TAG_AF_UNIX` and registers the path with the supervisor; `connect`
resolves the path through the VFS, so that permissions and symlinks apply, asks
the supervisor for the listener, and receives a pair of ring pipes with the
peer's pid, uid and gid. Stream sockets are byte rings; datagram and seqpacket
sockets are rings with a length prefix per message. Ancillary data travels in
the ring beside the bytes it belongs to, so `SCM_RIGHTS` is ordered with the
data exactly as Linux orders it: the sender duplicates the handles behind the
descriptors into the peer process with `NtDuplicateObject` (the peer's process
handle is opened by NT pid, which the same Windows user is allowed to do), then
writes a descriptor record (kind, flags, handle value) into the ring; the
receiver reconstructs the description from the record. D-Bus, `journald`'s
protocol, `sssd`, X11 over a socket, and every daemon that hands a client a
file descriptor need this, and nothing else on the system offers it. A socket
file created here is also visible to Windows programs and to WSL as a socket,
because the reparse tag is theirs.

Netlink exists for the one family glibc and the `iproute2` tools read:
`NETLINK_ROUTE` dump requests for links, addresses, routes, answered by the
supervisor from `GetAdaptersAddresses` with `GetIpForwardTable2`, so
`getifaddrs`, `ip addr`, `ip route` and `hostname -I` work. Writes to it are
`EPERM`. `/proc/net/tcp`, `udp`, `unix` are synthesised from the supervisor's
tables, which is what `ss` and `netstat` read. Name resolution is glibc's
resolver over `/etc/resolv.conf`, which the supervisor writes at start from the
host's DNS configuration, rewriting it when that changes, with `/etc/hosts`
seeded from the Windows hosts file.

### 12. IPC beyond pipes

SysV shared memory is an NT section named from the key under the installation's
prefix, `shmat` a view of it, `shmctl(IPC_RMID)` a note in the supervisor's
table that the segment dies with its last detach; SysV semaphores are the
supervisor's, with `SEM_UNDO` kept per process and applied at exit, including
the exit the supervisor performs for a crashed process; SysV message queues
likewise. PostgreSQL 10, which is el8's, uses the first two by default and the
third never. POSIX shared memory is a file under `/dev/shm`. POSIX semaphores
are futexes, since glibc implements them that way. POSIX message queues are the
supervisor's, with `mq_notify` delivered as a signal through the port of
section 4.7. `memfd_create` is an anonymous section that appears under
`/proc/self/fd/N` and can be `mmap`ed shared; sealing is accepted, enforced for
`F_SEAL_WRITE` by protection and for `F_SEAL_SHRINK` by refusing `ftruncate`,
and recorded as best-effort against a process that maps it writable before
sealing. `eventfd` and `pipe` between processes are section 4.9's.

### 13. Credentials: root is a number the kernel owns

A uid is a number in the process's credential block, initialised at launch from
the Windows user's entry in `/etc/passwd`, which the installer generates with
the Windows account at uid 1000 and `root` at 0. Because the DAC check in
section 4.8 is the kernel's, computed from EAs the kernel writes, uid 0 means
what it means on Linux inside the tree: it passes every mode check, `chown`s
freely, binds low ports (NT does not restrict them), reads any file the Windows
user can read. `setuid` and `setgid` bits on an executable change the effective
ids at `exec`, so `sudo`, `su`, `passwd`, `ping` with a setuid bit, and `rpm`
scriptlets that call `useradd` all work as written. `setresuid`, `setgroups`,
`capget`, `capset` and `prctl(PR_SET_KEEPCAPS)` are kernel-side with Linux's
rules; `capget` reports the full set for uid 0 and none otherwise, so a program
probing capabilities takes the path a root process takes. `CLONE_NEWUSER` with
a uid map is the same bookkeeping with an offset.

None of this is a privilege on Windows. Every process runs as the Windows user
with that user's token; a file the token cannot open stays closed however many
zeros the kernel holds. What is being supplied is the userland's expectation of
a root, which every el8 package's install scripts hold, which Cygwin's decision
to derive uids from SIDs and modes from ACLs could not supply. Where two
Windows users share an installation, each gets a supervisor, a pid space, and a
view of the same tree with the EAs saying whose files are whose, which is the
multi-user story of two machines sharing an NFS export, stated as such.

### 14. Time, entropy, `/proc`, `/sys`, `/dev`, and the remainder

`CLOCK_REALTIME`, `CLOCK_MONOTONIC`, `CLOCK_BOOTTIME`, `CLOCK_MONOTONIC_RAW`
read `KUSER_SHARED_DATA` at its fixed address: `SystemTime` and `InterruptTime`
in 100 ns units, plus the QPC bias for sub-tick resolution; so the vDSO's
`clock_gettime`, `gettimeofday`, `time`, `getcpu` complete without entering the
kernel, as they do on Linux. `CLOCK_PROCESS_CPUTIME_ID` is
`NtQueryInformationProcess`, `CLOCK_THREAD_CPUTIME_ID` its thread counterpart.
`nanosleep` and `clock_nanosleep` are alertable delays that return `EINTR` with
the remainder; `setitimer`, `timer_create`, `alarm` are the kernel's timer
thread over high-resolution waitable timers, delivering through the signal path
or a thread as `SIGEV_THREAD` asks; `adjtimex` and `clock_settime` are `EPERM`.

`getrandom` and `/dev/urandom` are a per-process ChaCha20 pool seeded with 256
bits from the supervisor at exec and reseeded on `fork` in the child, mixed
with `RDSEED` where the CPU has it; the supervisor's seed comes from
`BCryptGenRandom`. `/dev/random` is the same pool, since el8's own kernel no
longer blocks it either.

`/proc` carries what el8 programs read: `self/maps` from the VMA tree with the
file names the tree holds, `self/status`, `self/stat`, `self/statm`,
`self/exe`, `self/cwd`, `self/root`, `self/fd/`, `self/fdinfo/`,
`self/environ`, `self/cmdline`, `self/auxv`, `self/mountinfo`, `self/mounts`,
`self/limits`, `self/task/`, `self/oom_score_adj` (writable, ignored); `<pid>/`
for other processes from their process-info sections through the supervisor;
`cpuinfo`, `meminfo`, `stat`, `uptime`, `loadavg`, `version`, `filesystems`,
`mounts`, `net/`; and `sys/kernel/` with `osrelease`, `ostype`, `hostname`,
`pid_max`, `random/boot_id`, `random/uuid`, `sys/fs/file-max`,
`sys/vm/overcommit_memory`. `/sys` carries the cpu topology under
`devices/system/cpu/` that `sysconf(_SC_NPROCESSORS_ONLN)`, `nproc`, `hwloc`
read; `class/net/` from the supervisor; `kernel/mm/transparent_hugepage/`; an
empty `fs/cgroup/`. `/dev` carries `null`, `zero`, `full`, `random`, `urandom`,
`tty`, `console`, `ptmx`, `pts/`, `shm/`, `fd/`, `stdin`, `stdout`, `stderr`,
whatever the package set `mknod`s. Which further paths the acceptance set opens
is measured with `strace -e trace=openat` on el8 before any of the rest is
written; that census is the specification for the remainder.

`prctl` supports `PR_SET_NAME`, `PR_GET_NAME`, `PR_SET_PDEATHSIG` (the
supervisor raises it when the parent dies, which it observes),
`PR_SET_DUMPABLE`, `PR_SET_NO_NEW_PRIVS`, `PR_SET_KEEPCAPS`,
`PR_SET_CHILD_SUBREAPER`, `PR_SET_SECCOMP` and `PR_GET_TID_ADDRESS`; the rest
is `EINVAL`. `uname`, `sysinfo`, `getrlimit`/`setrlimit`/`prlimit` with
`NOFILE`, `STACK`, `AS`, `DATA`, `CORE`, `NPROC`, `CPU` (raising `SIGXCPU` from
the timer thread), `FSIZE` enforced; `sched_*` over NT priorities plus affinity
masks; `personality` stored and returned, otherwise ignored; `sync`, `syncfs`,
`fdatasync`, `fsync` as `NtFlushBuffersFile`; `sendfile`, `splice`, `tee`,
`copy_file_range` as kernel-side copies with a
`FSCTL_DUPLICATE_EXTENTS_TO_FILE` fast path on ReFS; `fallocate` as
`FileAllocationInformation` plus zeroing for the hole modes; `posix_fadvise`
accepted; `mlock`, `mlockall` as `NtLockVirtualMemory` with the working-set
quota raised first; `swapon`, `reboot`, `init_module`, `pivot_root`, `setns` on
a network namespace, `kexec_load` and `ioperm` as `EPERM`; `modify_ldt` and
`pkey_alloc` as `ENOSYS`.

### 15. Tracing, filtering, and dying well

`ptrace` is implementable because the kernel owns every thread.
`PTRACE_TRACEME`, `ATTACH`, `SEIZE`, `PEEKDATA`, `POKEDATA`, `PEEKUSER`,
`GETREGS`, `SETREGS`, `GETFPREGS`, `GETSIGINFO`, `CONT`, `SYSCALL`,
`SINGLESTEP`, `KILL`, `DETACH` and the `PTRACE_O_TRACE*` options map onto
`NtReadVirtualMemory`, `NtWriteVirtualMemory`, the substrate's `thread_context`
and `set_context`, the trap flag, the gate's entry and exit paths (which check
a traced flag and stop the tracee on its own signal port), and the supervisor's
process table for `wait` semantics. `int3` in user code becomes `SIGTRAP` to
the tracer. That is enough for `strace` in full, for `gdb` to attach, break,
step and inspect, and for `ltrace`. Cygwin's `gdb` works because `gdb` was
ported to NT's debug API; here `gdb` is el8's own, unported.

`seccomp` mode 2 is a classic-BPF interpreter at the gate, three hundred lines,
run over the syscall number and arguments before dispatch, with
`SECCOMP_RET_KILL`, `TRAP`, `ERRNO`, `TRACE`, `LOG`, `ALLOW`. It exists because
systemd-era packages set filters at startup, and fail closed when the call is
missing, not because it protects anything.

A fatal signal with `RLIMIT_CORE` above zero writes an ELF core with
`NT_PRSTATUS`, `NT_PRPSINFO`, `NT_AUXV`, `NT_FILE`, `NT_SIGINFO`,
`NT_X86_XSTATE` notes plus a `PT_LOAD` per VMA, which `gdb` opens as a Linux
core. `core_pattern` is a fixed name under the working directory.

### 16. The userland under each substrate

Under N the userland is rebuilt from el8's source packages with a toolchain
whose target differs from `x86_64-redhat-linux-gnu` in three places. GCC's x86
backend emits the thread-pointer load as `mov %gs:TP, %reg` instead of `mov
%fs:0, %reg` (one pattern in `i386.md`), defaults to `-mno-tls-direct-seg-refs`
so that no other `%fs` access is ever generated, and defaults
`-mstack-protector-guard-reg=gs -mstack-protector-guard-offset=TP+8`. glibc's
`sysdeps/unix/sysv/linux/x86_64/` loses its `syscall` instructions in favour of
the gate in `sysdep.h`, `clone.S`, `vfork.S`, `syscall.S`, the `*context.S`
trio, `__restore_rt`; its `tls.h` reads `%gs:TP`; its `configure` still sees
`linux-gnu`, because that is what it is. Nothing else in the distribution
changes; a post-link check in the RPM macros refuses any binary that still
contains an `0f 05` sequence in an executable segment or a `%fs`-prefixed
instruction, so that hand-written asm in a package fails at build time rather
than silently at run time. The triple's vendor field marks the target; the
`linux-gnu` suffix is kept because the kernel ABI is Linux's, so `configure`
scripts testing for it are right to believe it.

Under H the userland is Rocky Linux 8's own RPMs from its mirrors, installed by
`dnf` into the root, with a `kernel` package that is a no-op and a `systemd`
that installs but is never pid 1. No toolchain is ported, no package is
rebuilt; the acceptance suite is the distribution's own package tests.

### 17. What is borrowed, and from where

Every idea here that somebody else had first is listed with its source, so that
a reader can go and read the original, and so that the licence check DR-0074
asks for has a starting point. None of it is code; the kernel is written from
the specifications, and section 6 says what licence that leaves it free to
carry.

From Cygwin, at `6b7a25ed9`: the open-file-description class hierarchy
(`fhandler`), the escaping of NTFS-illegal characters into `U+F000`
(`path.cc:515`), the WSL symlink form (`path.cc:1976`), the POSIX-semantics
delete and rename calls (`syscalls.cc:768`, `:2740`), the thread-hijack
delivery with its red-zone lesson (`exceptions.cc`, `sigdelayed`, and DR-0030
of this project), the `select_stuff` fan-out past 64 handles, the
`installation_key` that keeps two installations from sharing named objects
(`mm/shared.cc:50`), the Windows argument-quoting rules for `exec` of a PE, the
`/proc` synthesis, the `peek_pipe` polling for foreign pipes, and, as a
negative example, the copying `fork` (`fork.cc:366`).

From WSL1, as Microsoft documented it: the LX metadata EAs and reparse tags,
`FileStatLxInformation`, per-directory case sensitivity, `/mnt/<drive>` mounts,
the interop model in which a Windows child is created by init and its handles
pumped, the ConPTY bridge, the `WSLENV`-style environment translation, the
4.4-series `uname` that told packages what to expect.

From Interix and the NT POSIX subsystem before it: the native-subsystem process
that never registers with `csrss`, and `fork` as an executive clone. From
gVisor: the platform interface, the sentry-owns-the-mm model, its syscall test
suite as a second certification corpus. From `wepoll`: `IOCTL_AFD_POLL` as the
readiness primitive. From Wine: its public reimplementation of AFD and ConDrv
as a reading guide to both. From musl: the syscall wrappers as the clearest
reading of the ABI's argument conventions. From Linux itself: the man pages,
LTP and the UAPI headers as the specification; `binfmt_elf`, `n_tty`,
`pipe_fs_i`, `fs/namei.c` as behaviour to match rather than code to copy.

### 18. Order of work

Phases are ordered so that the substrate-level bets are settled before a line
of the core depends on them; then by what each later phase needs; then by
demand. Each phase names the criterion in section 7 that gates it.

Phase 0, the measurements. Six spikes in `spike/`, each with a transcript, run
before any design commitment: (a) `nt-clone-fork`, whether `NtCreateProcessEx`
cloning works on Windows 11 23H2 and 24H2 for a native process with inherited
handles, section views, a second thread created after the clone, CET disabled,
with the time per clone against Cygwin's `fork` on the same machine; (b)
`native-host`, whether a `ntdll`-only process can be created from a Win32
parent with `NtCreateUserProcess`, and from `cmd.exe` with `CreateProcess`, can
create and use an AFD socket, `RtlWaitOnAddress` and an ALPC port, and can be
injected into by the endpoint product on the operator's machine without dying;
(c) `arena`, placeholder reservation of the user range, section views replaced
into it at 64 KB, lazy commit through a vectored handler, with the cost per
first-touch fault; (d) `hijack`, asynchronous delivery into a spinning thread
by context rewrite on Windows 11, with the in-kernel flag honoured, and the
latency; (e) `lxfs`, `FileStatLxInformation`, the EAs, LX symlinks,
case-sensitive directories, POSIX delete on the operator's NTFS volume, and the
`stat` rate against Cygwin's; (f) `whp`, a partition running a ring-3 loop that
executes `syscall` and takes a page fault, with the exit latency, on the
operator's machine with the feature enabled. Gate: each spike's transcript,
with open question 1 answered from (f). Two weeks of work; the whole proposal
is conditional on (a) through (e).

Phase 1, a process. `lk-host` with the gate and kernel stacks, the VMA tree and
arena, `binfmt_elf`, the initial stack and auxv, the vDSO, `exit_group`,
`write` to a foreign handle. A static test program written against the gate
prints and exits. Gate: criterion 1.

Phase 2, files. The VFS, the mount table, the root and `/mnt`, `open` through
`getdents64`, `stat` in its forms, `dup`, `fcntl`, `pipe`, `/proc/self`,
`/dev`. Gate: criteria 2, 3.

Phase 3, the ported glibc and the supervisor. The N toolchain change, the
glibc `sysdeps` port, `ld.so` loading a dynamic program; `lk-init` with pids,
`fork` by clone, `execve` in place, `wait4`, signals with delivery in all four
ways, `kill`, the launcher, PE `exec` through the supervisor. `bash` runs a
script that forks, pipes, redirects and traps. Gate: criteria 4, 5, 6.

Phase 4, threads. `clone`, futexes, robust lists, `tgkill`, thread exit,
`/proc/self/task`. glibc's `nptl/` tests pass. Gate: criterion 7.

Phase 5, terminals. Ptys and the line discipline in the supervisor, the bridge,
job control, `/dev/pts`. An interactive `bash` under Windows Terminal with
`^Z`, `fg`, `vim`, `less`, `tmux`. Gate: criterion 8.

Phase 6, readiness and sockets. `epoll` and its family, `eventfd`, `timerfd`,
`signalfd`, `inotify`, AFD sockets, `AF_UNIX` with `SCM_RIGHTS`, netlink dumps,
the resolver's files. `curl`, `ssh`, `python3`'s asyncio tests, `nginx`, D-Bus.
Gate: criteria 9, 10, 11.

Phase 7, IPC and locks. SysV IPC, POSIX IPC, the lock table, `memfd`.
PostgreSQL 10's `initdb` and regression suite. Gate: criterion 12.

Phase 8, tracing and the remainder. `ptrace`, `seccomp`, cores, rlimits,
`prctl`, the `/proc` census. `strace ls`; `gdb` breaking in a signal handler.
Gate: criteria 13, 14.

Phase 9, the LTP run and the differential table, then the package rebuild under
N or the RPM install under H, which is the acceptance project this proposal
hands on. Gate: criteria 15, 16.

Phases 1 and 2 are one person's path; 3 splits into the toolchain and the
supervisor, which are independent; 5, 6 and 7 are independent of each other
once 3 and 4 stand. H, if chosen, replaces the gate and the arena in phase 1
and the toolchain half of phase 3, and touches nothing from phase 4 on.

## Alternatives considered

Keep emulating at the C library boundary, which is DR-0000 and everything built
on it. It is the design that exists; its measured price is the veneer, the
divergence kinds, the renumbering generators, a port of glibc whose `sysdeps`
backend is written against Cygwin's bodies rather than against a syscall table;
0010 sections 3 through 5 are the plan for paying it, which does not reach
`ptrace`, a fast `fork`, root, or a kernel-side pty at any price. The killing
trade-off is that every constant, layout and errno glibc exposes has to be
reconciled with Cygwin's by hand or by generator, forever, one kind at a time;
the reconciliation never removes the second implementation it reconciles with.
Rejected as the destination; what it built is inventoried in section 6.

A syscall dispatcher over Cygwin: catch the number, dispatch to a Cygwin body.
0010 rejected it as adding a calling convention without removing body work, and
that reasoning holds. What it missed is the other direction: a dispatcher over
bodies written for the syscall boundary is not an addition, it is the kernel;
the body work it "adds" is the work 0010 was already scheduling under the names
S2 and S3. Rejected in 0010's form, adopted in this one.

Dynamic binary translation on the native substrate, so that el8's shipped
binaries run without a rebuild: DynamoRIO or a Blink-style JIT rewriting
`%fs` references and `syscall` instructions in a code cache. It is a third
substrate and it fits the interface in section 4.3. It is not proposed
because the two costs it removes, the toolchain change and the rebuild, are
each bounded and each a known quantity, while a code cache under a garbage
collector or a JIT is neither, and because the hypervisor substrate removes
the same two costs with hardware doing the translation. Kept as a named
option for a host where the hypervisor feature is refused.

A kernel driver: a pico-provider in the shape WSL1 used, or a filter that
intercepts `syscall` from tagged processes. It would give substrate H's
fidelity at substrate N's cost. It is not available: the pico interface is not
public; a driver needs an EV-signed attestation to load on Windows 11; HVCI on
the operator's machines refuses anything else. Rejected on deployability.

One supervisor holding the whole kernel, every process a thin client over ALPC;
gVisor's sentry as a separate process, or Wine's server. It makes every `read`
a round trip, five to ten microseconds against a function call, and puts the
hot path of every program behind one process's scheduler. The split here keeps
the per-process kernel in the process and sends only what crosses processes to
the supervisor, which is what Cygwin's shared-memory design was reaching for
without a process to own the state. Rejected for the hot path; adopted for the
cold one.

No supervisor at all, Cygwin's shape: cross-process state in a shared section
that every process updates cooperatively. It is the source of Cygwin's hardest
defects, a process that dies with a lock held or a pid slot half written; it
cannot do exit cleanup for a process that was terminated, which is why Cygwin's
`SIGKILL` leaks. A process that owns handles to every other process can.
Rejected.

NT named pipes as the transport for Linux pipes, with polling for readiness,
which is Cygwin's choice and 0010's. Rejected for the reason in section 4.9;
kept for foreign handles, where there is no choice.

Winsock through `ws2_32.dll` rather than AFD. Needs `kernel32`, which needs
`csrss`, which the host process must not have if `fork` is a clone. Rejected
on that dependency alone; the readiness gain is a consequence.

ACL-derived permissions and SID-derived uids, Cygwin's model. Exact for a
Windows administrator's view of a file, unable to express a root, a setuid bit,
a `chown` to a user that does not exist on the machine, or a mode NT's ACL
grammar cannot state. The EA model expresses all four; it is the format the
rest of the Linux-on-Windows world reads. Rejected.

Windows's own `afunix.sys` for `AF_UNIX`. Stream only, no descriptor passing,
no credentials, no datagrams; every daemon in the acceptance set that uses
`AF_UNIX` needs at least one of those. Rejected, with its reparse tag adopted
so that a socket file is recognised across the boundary.

Copying `fork`, Cygwin's, as a fallback should the clone spike fail. A fallback
doubles the process model and halves the confidence in either half. If spike
(a) fails on a supported Windows build, this proposal is amended by an addendum
rather than a fallback, and the amendment is a different proposal.

WSL. A Linux kernel in a virtual machine is the best Linux kernel emulation
possible; the operator excluded it. What this design keeps from WSL2's shape is
the interop model and the file format; what it keeps from WSL1's is the idea
that the kernel personality is a layer above NT rather than a guest beside it.

## Cross-cutting concerns

Standing decisions this reopens. DR-0000 (the Cygwin re-faced floor) is
replaced outright. DR-0003 and DR-0021 (the TLS model and its carrier) survive
under substrate N with a narrower hazard and are moot under H. DR-0005 (the
bounded Linux claim) is inverted: the `syscall` instruction is not reached
under N and is the interface under H; `linux` in the triple is a claim about
the kernel ABI that is now true everywhere. DR-0006, DR-0030 and DR-0050 (red
zone and delivery shape) carry forward unchanged; the design delivers as they
specify. DR-0004 and DR-0037 (licence) are reopened by the next paragraph.
DR-0088 through DR-0094 (errno numbering and the floor-only block) become
vacuous, since there is one numbering. Every decision about the veneer, the
faces, the wiring, the version nodes, the low window (DR-0041 through DR-0049,
DR-0051 through DR-0058, DR-0065 through DR-0072, DR-0079 to DR-0083, DR-0085
to DR-0087, DR-0090) describes a bootstrap that the new design does not pass
through; they are retired together by the ratifying record, not amended. The
spike transcripts under `spike/` remain measurements of this Windows, cited as
such.

Licence. The kernel takes no code from Cygwin; the proposal's borrowed list is
of ideas and formats; the tree that results is therefore not a derivative of
`winsup`, so DR-0004's inheritance does not apply to it. The operator chooses
the licence; the recommendation is one the userland's own packages already link
against without thought, so LGPL-2.1-or-later, which is glibc's, or a
permissive licence. What the kernel reads while being written is GPL in places
(LTP, the Linux sources); that is reading, which DR-0074's check distinguishes
from lifting; where a body's shape is taken from a GPL file rather than from a
man page, the record says so and the body is rewritten from the page.

Compatibility with what exists. Nothing in `veneer/`, `loader/`,
`runtime/winsup/` or the vendor tree's `rhelcyg` branch is used. The toolchain
work under `toolchain/`, the target-triple record, the demand census and the
acceptance framing in `acceptance/` carry over to substrate N unchanged in
purpose, since a rebuilt userland is still a rebuilt userland. Under H they are
retired too. The build environment DR-0038 names remains the build environment.

Host version floor. `NtAllocateVirtualMemoryEx` placeholders,
`FileStatLxInformation`, case-sensitive directories, `afunix`'s reparse tag,
high-resolution timers arrived in Windows 10 1803; POSIX delete and rename in
1709; `RtlWaitOnAddress` in Windows 8. The floor is Windows 10 1809 for
function, Windows 11 for certification, recorded in `target-definition.md`'s
successor; Windows Server 2022 or later is in the certified set once a spike
runs there.

Undocumented interfaces. The design rests on `NtCreateProcessEx` cloning, AFD,
placeholder replacement of section views, `FileStatLxInformation`, and the
TEB's `TlsSlots` offset. Each is used by shipping Microsoft software or by
Windows itself; each has been stable across every release the project can
measure; each gets a presence test in the gate tier that fails the day it
changes, so that a Windows update is diagnosed by a red check rather than by a
hang. That is the same footing Cygwin has stood on for its own use of
`NtQueryDirectoryFile`, `NtSetInformationFile` and the rest for twenty years.

Endpoint protection. A native process with no `kernel32` and a cloned address
space is unusual, and section 4.4 keeps every executable mapping file-backed so
that nothing looks like injected code. Spike (b) measures the operator's
endpoint product against the host process before anything else is built; the
result is a deployment constraint recorded alongside `AGENTS.md`'s existing
note about executable memory.

Idempotency. The installer creates the root, sets case sensitivity, writes the
mount table, generates `/etc/passwd`, `/etc/group`, `/etc/resolv.conf`,
`/etc/hosts` from templates plus the host's state, and registers the launcher
links; a second run reseeds each generated file and removes any link it no
longer manages, in the shape DR-0034's manifest already prescribes. The
supervisor is started on demand and is idempotent to start twice, since the
second finds the first's port and exits.

Failure and rollback. A supervisor crash is a kernel panic for that user's
session: every `lk-host` finds its port gone at the next message and exits with
`SIGKILL` semantics, which is what the job object enforces in any case. A
process crash is cleaned by the supervisor. The on-disk tree carries no format
of this project's own, so uninstalling is deleting the directory; a tree
written here is intact under WSL or Cygwin afterwards. There is no migration
from the current design's artifacts, since none are persisted in a form this
design reads.

Security surface, stated so that nobody mistakes it. A Linux process here is
the Windows user, with the Windows user's token; everything else is
bookkeeping. The named objects the design creates, signal ports, pipe sections,
IPC sections, the supervisor's port, are ACLed to the creating user, so a
second user on the machine cannot signal or read them. Descriptor passing
duplicates handles between processes of the same user, which that user could do
anyway. `setuid` changes a number in the process, never the token.

## Verification criteria

Each is a command that exits zero or a state a script can check, registered
in `test/suites.tsv` at the named tier.

1. `test/t/hello-static.sh` (gate): a static test program calling the gate
   directly prints `hello` and exits 0 under `lk-host`; its `/proc/self/maps`
   as written by the kernel lists the program, the stack, the vDSO and nothing
   else.
2. `test/t/vfs-diff.sh` (report): a scripted sequence of file operations
   (create, write, rename over open, unlink while open, symlink, hard link,
   chmod, chown, utimensat, mkdir, rmdir, getdents64, statx) produces the same
   trace of results and errnos here as on the el8 reference, over the root;
   over `/mnt/c` the differences are exactly the recorded ones.
3. `test/t/lxfs-interop.sh` (report): a tree written by the kernel is read by
   WSL1's DrvFs with metadata and by Cygwin 3.6 with identical modes, owners,
   symlink targets and special files; the reverse holds for a tree written by
   WSL.
4. `test/t/fork-bench.sh` (report): `fork` of a process with 200 VMAs and 64
   descriptors completes; the child sees the parent's descriptors and a
   `MAP_SHARED` write from either side; the median of 1000 iterations is under
   2 ms on the operator's machine, with Cygwin's `fork` on the same machine
   recorded beside it.
5. `test/t/exec-shape.sh` (report): `execve` keeps the pid, the supervisor's
   handle, the non-`CLOEXEC` descriptors and the console; `argv`, `envp` and
   the auxiliary vector reach the new image byte-identical to el8's for the
   same command, `AT_SYSINFO_EHDR` excepted.
6. `test/t/signals-diff.sh` (report): delivery on each of the four paths,
   `siginfo` contents, `SA_RESTART` behaviour per syscall, `sigaltstack`,
   real-time queueing, `SIGCHLD` codes, `SIGSTOP`/`SIGCONT` of a stopped
   process, `SIGKILL` of a process spinning with signals blocked, each
   identical to the el8 reference; a spinning thread receives an asynchronous
   signal within 1 ms.
7. glibc 2.28's `nptl/` and `posix/` test directories, built for the target,
   pass at the rate the el8 build of the same tree passes on the reference,
   with each failure named in the transcript. Tier report.
8. `test/t/tty-diff.sh` (report): canonical editing, `ISIG`, job control (`^Z`,
   `fg`, `bg`, `TOSTOP`), `SIGWINCH`, `VMIN`/`VTIME`, and the `termios`
   round-trip match el8 through a pty; an interactive `bash` under Windows
   Terminal survives the script in `test/t/tty-session.txt`.
9. `test/t/epoll-diff.sh` (report): the ordered event list for a scripted
   `epoll_ctl`/`epoll_wait` sequence over pipes, `eventfd`, `timerfd`,
   `signalfd`, a socket pair and a listening socket matches el8 for level and
   edge triggering, `EPOLLONESHOT` and `EPOLLEXCLUSIVE`; a foreign-handle
   descriptor is the only one whose wake is late, by at most the poll interval.
10. `test/t/sockets-diff.sh` (report): TCP, UDP and `AF_UNIX` in all three
   types, with `SCM_RIGHTS` and `SO_PEERCRED`, `MSG_PEEK`, non-blocking
   connect, `SO_REUSEADDR`, dual-stack, `getifaddrs`, each matching el8 call
   for call; `curl https://` and `ssh localhost` succeed.
11. `test/t/inotify-diff.sh` (report): every `IN_*` event including
   `IN_CLOSE_WRITE` from a Linux writer matches el8 in name and cookie; a
   Windows writer yields the recorded subset.
12. PostgreSQL 10 as packaged by el8: `initdb`, `pg_ctl start`, `make check`
   pass. Tier report.
13. `strace -f -o trace ls -la /` produces a trace that, after pid and address
   normalisation, matches the el8 trace line for line; `gdb` attaches to a
   running process, breaks in a signal handler, steps, and prints a backtrace
   through the `rt_sigframe`. Tier report.
14. `test/t/proc-census.sh` (report): every `/proc` and `/sys` path the
   acceptance set's `strace` census opened is readable with the fields the
   census saw used.
15. LTP's `syscalls` runtest file runs to completion; the pass rate and the
   per-test diff against a run on the el8 reference are published in
   `doc/status/ltp-<date>.tsv`; no test that passes on el8 crashes the kernel
   here. Tier standalone.
16. `bin/check-nt-surface` (gate): each undocumented interface in the
   cross-cutting section has a presence probe that passes on this host.
17. `bin/check-design-links` and `bin/check-doc-refs` exit 0 with this
   proposal's record cited from every governed section it replaces.

## Open questions

1. The substrate. Phase 0's spike (f) prices a WHP exit on the operator's
   machine; spike (a) prices a clone. If a syscall under H costs under 10 µs
   and the operator accepts the hypervisor feature as a prerequisite, H is the
   better kernel emulation by every measure but syscall latency, and it removes
   the toolchain and rebuild projects entirely; if either condition fails, N.
   Both are designed; one is built first; the interface in section 4.3 is what
   keeps the second from being a rewrite.
2. Whether `NtCreateProcessEx` cloning is sound on Windows 11 24H2 with Control
   Flow Guard, user-mode shadow stacks off, and the operator's endpoint product
   loaded. Interix proves the primitive; nothing this project has run proves it
   on this build. Spike (a).
3. Whether a `ntdll`-only process survives the operator's environment. Endpoint
   products inject DLLs that import `kernel32`; a process with no `kernel32`
   either loads it on demand (whereupon `csrss` registration fails in a clone)
   or refuses the injection. Spike (b) finds out which.
4. The language. A kernel-class codebase that parses untrusted ELF, walks
   untrusted paths and juggles handles across processes is the case for Rust
   with `no_std` on `ntdll` alone, which has been done for native Windows
   tooling; C with a freestanding toolchain is the case for reading ease
   against Cygwin, Linux and the NT references, all of which are C. The
   recommendation is Rust for the kernel and the supervisor, with C for the
   trampolines and the ring-0 shim; the operator decides.
5. The licence, per the cross-cutting section.
6. Whether the thread pointer word under N is a `TlsSlots` index (cheaper by
   one instruction, hazard reduced but not gone) or the below-stack word
   DR-0003 chose. Either satisfies section 4.6; the choice is a constant.
7. Which `/proc` and `/sys` paths the acceptance set reads. Unmeasured, and
   phase 8's census is the measurement.
8. Whether `IOCTL_AFD_POLL` reports edge transitions reliably enough for
   `EPOLLET` on sockets, or whether the kernel keeps a last-state per interest
   and derives edges itself. `wepoll` does the latter; criterion 9 decides
   whether it is needed.
9. Whether the supervisor should be one per user or one per user per
   installation root. One per root is Cygwin's `installation_key` rule, which
   keeps two trees from sharing a pid space; one per user lets a process in one
   tree signal a process in another. The proposal assumes per root.
10. The name. `lk-` is a placeholder throughout.

## Not verified

Phase 0 ran on 2026-09-04 and this section is superseded for seven of the
claims below, each of which now has a transcript: the clone, the placeholder
and its section views, `FileStatLxInformation` through
`NtQueryInformationByName`, `RtlWaitOnAddress`, AFD's poll, the ntdll-only
process created from `cmd.exe` (refused, and the design does not need it), and
WHP's exit latency. The decision log at the end of this document reads each
against the paragraph it retires, and the paragraph is left standing so a
reader can see what was assumed before it was measured. What follows is the
draft's own text, unedited.

Every NT behaviour this proposal leans on is a reading of documentation, of
ReactOS, of Wine, of `wepoll`, or of Cygwin's source at `6b7a25ed9`, not a
measurement: that `NtCreateProcessEx` with a null section clones on Windows 11;
that placeholders accept a section view at 64 KB inside a reservation of tens
of terabytes; that `FileStatLxInformation` returns the four LX fields through
`NtQueryInformationByName`; that `RtlWaitOnAddress` behaves as `FUTEX_WAIT`
under contention; that AFD's poll request reports the readiness `epoll` needs;
that a `ntdll`-only process can be created by `CreateProcess` from `cmd.exe`;
that WHP's exit latency on the operator's machine is in the range quoted. Phase
0 exists to turn each of these into a transcript; the proposal is conditional
on all of them.

The constants quoted for reparse tags and information classes are from
`winnt.h` or `ntifs.h` as recalled, checked against Cygwin's use of
`IO_REPARSE_TAG_LX_SYMLINK` at `path.cc:1954` and nothing else this session;
the rest are re-read from the headers before any code uses them.

The claim that el8's shared objects satisfy the 64 KB congruence rule is a
reading of binutils 2.30's default `max-page-size` for x86-64, not a census of
the package set; `spike/vendor-image-shape/` measured some of it, and a full
census is phase 2's first job.

The timings given as targets (2 ms per `fork`, 5 to 15 µs per WHP exit, five
cycles per thread-pointer access) are, in order, a target, a recollection of
published gVisor and WHP measurements, and a measurement from
`spike/gs-thread-pointer/`. Only the last is this project's.

That WSL2 still honours the DrvFs metadata EAs when a Windows directory is
mounted into it is a reading of Microsoft's documentation as of the training
data behind this draft, not a test. Criterion 3 is the test.

Interix's use of the executive clone for `fork` is from published accounts of
the POSIX subsystem's design, not from its source, which was never public.

## Phase 0 decision log

Run 2026-09-04 on `ins-15`, Windows 11 `10.0.26200.9168`, under the operator's
instruction to execute phase 0 without stopping. Six spikes, one git worktree
and one branch each, dispatched concurrently because no two share an edge;
integration serial, one merge at a time. Each entry names the tier of
`decision-ladder.md` that discriminated. Milestones rows 35 to 40 carry the
findings; the transcripts under `spike/` carry the evidence.

D1. Whether phase 0's six spikes run concurrently. Tier 7. They share no
source, no build product and no state, and each writes only its own directory,
so the only coupling is the host they measure on. Timing-sensitive cases could
have bent under the load of five siblings, and the reproducible-spike contract
answers that directly: a timing is never a finding, and each spike reruns to
confirm its verdict words. Every one of the six reran and matched.

D2. Whether spike (a)'s first verdict stands. Tier 1, on the ladder's own
rule that an empirical tier narrows only after every surviving candidate is
measured. The first pass measured `NtCreateProcessEx` with a null section,
found the address space cloning and no thread creatable in the clone, and that
is a design-ending result for a proposal whose process model is `fork`. It was
one candidate of several. `RtlCloneUserProcess` was measured next and works;
`NtCreateUserProcess` with `PROCESS_CREATE_FLAGS_INHERIT_FROM_PARENT` was
measured and is refused; the hypothesis that the parent's `csrss` registration
was to blame was measured from an ntdll-only native parent and is disproven.
The correct verdict is that the clone works through the wrapper, and the
proposal's open question 2 is answered yes. Recording the first pass would
have been a guess with a transcript behind it, which is the failure the rule
exists to prevent.

D3. Whether spike (b)'s AFD half stands as a partial. Tier 1. A refused open
packet is a fault in the packet, not a verdict about the driver, and the
proposal's own "Not verified" list names AFD's readiness as a load-bearing
claim. Two layout faults were behind the one `STATUS_INVALID_PARAMETER` — the
EA name is the literal `AfdOpenPacketXX`, and this build's
`IOCTL_AFD_CONNECT` wants `AFD_CONNECT_JOIN_INFO` — and past them the poll
reports the readiness `epoll` needs, from inside a process whose loader list
still holds two modules. Wine, ReactOS and `wepoll` were read as
specification; nothing was lifted, per `AGENTS.md`'s rule and DR-0074's check.

D4. What to do with spike (c)'s 4 KB result. Tier 8, parked, not taken. The
placeholder mechanism replaces views at 4 KB on this build where § 4.4 assumed
64 KB, which would loosen the `p_vaddr ≡ p_offset (mod 64 KB)` congruence rule
and with it the `MAP_PRIVATE` fallback and the `EINVAL` for a non-congruent
`MAP_SHARED`. 64 KB is what Microsoft documents and 4 KB is what this one
kernel does; both candidates survive the measurement, and choosing between a
documented invariant and a measured behaviour on a single machine is a value
call about how much of the design to hang on one host. The spike's reading —
build against 64 KB, let the kernel probe for the finer granularity — is a
recommendation and is recorded as one. The operator decides.

D5. The substrate, open question 1. Half settled, half parked. The empirical
half is measured and closed: a syscall under substrate H costs a median 5.0 µs
on this machine against 679 ns for a trapping NT syscall, which clears the
proposal's own 10 µs threshold, so the condition the proposal set is met and
the recalled 5 to 15 µs range holds at its low end. The other half — whether
the operator accepts the hypervisor feature as a prerequisite — is a value and
is tier 8 by construction. Nothing in the autonomy grant reaches it. Both
substrates remain designed; which is built first is the operator's call, now
evidenced.

D6. Whether criterion 3 is amended. Tier 8, parked. A tree carrying WSL's
metadata reads correctly under WSL and not under Cygwin 3.6.10, which reads
both files 755 owned by the Windows account and a character device as an empty
regular file. Criterion 3 as written requires both, so as written it cannot
pass on this host. Whether the criterion narrows to WSL, or keeps Cygwin and
accepts a recorded divergence, or is dropped, changes what the project has
promised to verify. That is the operator's, and the spike reports the fact
without touching the criterion.

D7. Where the spikes land. Tier 7, and it carries a residue worth naming. The
six branches were first merged straight onto `main`, which is wrong here:
`march` is the trunk and `bin/session-land` is the mechanism, with `main`
fast-forwarding behind it. Undoing six local merge commits wants a ref move,
and that capability is withheld from this agent, so the recovery was
forward-only — `march` fast-forwarded to `main`, and this change landed
through the session flow on top. The content is correct and nothing was
discarded or rewritten. What remains is cosmetic: `main` and `march` carry six
merge commits where a single session-land merge belongs. None of it has been
pushed, so the operator can collapse them with one reset if the history
matters; this entry exists so the choice is theirs rather than hidden.

D8. What phase 0 does not answer, stated so nobody reads more into it. Spike
(d)'s hijack finding is bounded to a host with user-mode shadow stacks off,
which is this one; a host with CET on is not covered. Spike (b)'s injection
half is answered for Windows Defender only, this machine carrying no
third-party EDR, and the `AGENTS.md` note about endpoint products stands
unchanged. Spike (c) refuses a 127 TB placeholder, so § 4.4's claim on the
whole user range fails and the practical claim — half of it in one
reservation — is what holds. Open question 8, edge against level triggering,
is reported from spike (b) as level-triggered and left to criterion 9. None of
these is a blocker for phase 1; each is a bound on a sentence somebody will
otherwise read as unconditional.

## Addendum, 2026-09-06

Criteria 3 and 4 above are amended by DR-0102: criterion 3 reads the tree
back under WSL only, and criterion 4 is per substrate, 2 ms under H and a
recorded median with a regression band under N. Open question 1's second
condition is answered by the same record: both substrates are offered and
the client's environment decides. Open question 6 is settled by DR-0101
(`TlsSlots[63]`, reserved through the PEB bitmap). Proposal 0012 completes
§ 3 to § 7 where they spoke for one substrate.
