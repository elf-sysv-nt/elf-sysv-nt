# Architecture

What this system is, in the present tense. A reader who wants to know why a
thing is the way it is goes to `doc/design/decisions/`; a reader who wants to
know what is built goes to `doc/status/`. Proposal 0011 is the design of
record, completed by proposal 0012 where 0011 was silent or spoke for one
substrate only; both are accepted, and this document is the map over them,
not a summary of them. Where a proposal settles something in detail, the
section here says what the shape is and points at the section that carries
the detail.

A section a decision record settled ends in one line naming the records.

## What this document covers

The platform runs unmodified or rebuilt el8 userland on Windows by emulating a
Linux kernel at the `syscall` boundary. Everything above the substrate line is
that kernel; everything below it is one of two substrates that give the kernel
an address space and threads to run user code on.

That is a change of boundary, not a change of degree. The arc before it put a
veneer at the C library's boundary, presenting a glibc ABI that was not glibc
over a re-faced Cygwin, and none of it is used here. `veneer/`, `loader/`,
`runtime/winsup/` and the vendor tree's `rhelcyg` branch are absent from this
repository by construction; `doc/history/the-veneer-arc.md` says where they
went and why a citation written `veneer:doc/...` means the sibling rather than
a broken path. The records that governed that arc are retired in place rather
than amended, which is the difference between a subject that left and an
argument that was wrong.

What survives the change is everything that was about el8 and this host rather
than about the veneer: the target triple, where el8 source comes from, the
toolchain defaults, the TLS model, the loader's shape, and the phase-0 spikes,
which measured this Windows and are cited as such.

Settled by: DR-0097.

## The shape of the system

One kernel, two substrates, and two seams beneath the kernel on different
axes: the substrate interface, which is how user code is run, and the process
seam, which is where state that crosses Linux processes lives.

The **core** is the Linux personality: the syscall table, the process and
thread model, signals, the VMA tree, the VFS, and the descriptor layer. It is
written once and knows nothing about NT. `bin/check-substrate-line` enforces
that mechanically — nothing above the line may name `Nt*`, `WHv*`, `CONTEXT`,
an NT `HANDLE` type, or dereference a user pointer — and it walks `core/` on
every gate run.

The **process seam** is what one Linux process cannot own: pid allocation
and parent links, process groups and sessions, wait queues and exit status,
signal send to another process, ttys and the line discipline, the `AF_UNIX`
rendezvous namespace, the SysV and POSIX IPC namespaces, advisory locks, the
inotify registry, process-shared futex queues, and `SIGKILL`, `SIGSTOP` and
`SIGCONT` applied to a process that may not be responding. The core calls
it as an interface; its realisation is chosen with the substrate. Under N it
is the supervisor, `lk-init.exe`, reached over ALPC with a shared page and
named sections, because under N every Linux process is an NT process and
nothing else can hold the table. Under H it is a set of tables in the one
kernel process, and the calls are calls. The core is written against the
seam and never against ALPC by name, for the same reason it is written
against the nine calls and never against `Nt*`.

The core never hands a host call a user address. Every byte that moves
between user memory and a host object goes through `user_copy_in` or
`user_copy_out` and a kernel buffer, in chunks; Phase 1's `write` is the
pattern. Under N a user buffer in a lazily committed range fails inside the
host call, since the I/O manager probes it in kernel mode where no vectored
handler runs (spike 49: `ERROR_NOACCESS` on read, `ERROR_INVALID_USER_BUFFER`
on write, handler entered zero times); under H a user address is a guest
virtual address no host call can take. The rule holds for both substrates and is the reason the host glue
takes kernel pointers and lengths and never a user address.

A **substrate** is the nine calls the core needs from whatever is underneath:
`as_map`, `as_unmap`, `as_protect`, `as_clone`, `thread_start`,
`thread_interrupt`, `thread_context`/`set_context`, `tp_set`, and
`user_copy_in`/`user_copy_out`. `doc/design/Substrate-Interface.md` is the
contract, with a per-call conformance bar written before any implementation
existed.

**Substrate N** is native: user code runs as ordinary NT threads in the host
process, reached through a gate rather than a `syscall` trap, because NT
cannot trap `syscall`. It is built and certified 9/9 against that bar —
`doc/design/Substrate-N.md` records the mechanisms and the decision log — and
it needs a rebuilt userland, since a shipped el8 binary reaches the kernel with
an instruction N does not see.

Where the two substrates answer a question differently, the section says which
one it means. That is not padding: the thread pointer sits in a different
segment register under each, and a sentence that leaves the substrate out is
wrong about one of them.

**Substrate H** is the hypervisor: WHP vCPUs running shipped el8 binaries
unmodified, at about five microseconds a syscall against N's one. Its shape
is one kernel process per Windows user per installation root,
`lk-kernel.exe`, a Win32 process because `WinHvPlatform.dll` needs
`kernel32`, holding one partition; a Linux process is a page-table root in
guest physical memory, a Linux thread is a task that runs on a vCPU from a
pool while in user mode and holds an NT thread of its own for the kernel's
waits. The shape is measured rather than chosen: a process may hold one
mapped partition at a time on this host (spike 43), so a partition per
Linux process inside the kernel process is not available, and a host
process per Linux process was measured (spike 45) and declined for its fork
cost. H is designed, its nine calls are each backed by a landed spike, and
it is not built. The two substrates exist because different deployments
need different ones, and the choice is not always the fast one: a client
environment with nested virtualisation outside support forces N whatever
the measurements say, and a client that needs stock el8 binaries forces H.

Settled by: DR-0098.

## The gate, and what has run

Under N there is no `syscall` trap, so the kernel publishes a gate address in
the auxiliary vector as `AT_SYSINFO` and user code reaches it with a plain
`call`. The register convention is the kernel's: number in `%rax`, arguments in
`%rdi %rsi %rdx %r10 %r8 %r9`, result in `%rax`, with `%rcx` and `%r11`
clobbered. The gate switches to a per-thread kernel stack before running any
core frame, captures the call, and restores the user state on the way out.

A syscall interrupted by a signal restarts through a vDSO stub under N,
because there is no two-byte instruction to back `%rip` over: the gate saves
the number at entry as `orig_rax`, and when its exit path finds a restartable
result with a handler to run, the signal frame names `__lk_restart` (`jmp
*_lk_gate`) as `%rip`, the gate-entry `%rsp` still holding the original
return address, and `%rax` reset from `orig_rax`; `rt_sigreturn` lands in
the stub and the stub re-enters the gate with the stack exactly as the
original `call` left it. Under H the instruction is real and the restart is
Linux's own rewind. `ptrace` reads `orig_rax` from the gate's saved word
under N and from the shim's spill under H.

Phase 1 of the core is built and runs: a static ELF written to that ABI is
mapped through the substrate, entered on the psABI initial stack with its
auxiliary vector, calls `write` and `exit_group`, and prints `hello` before
exiting 0 — checked against the same logical program on a Rocky 8 oracle.
`doc/design/Core-Phase1.md` records it.

Phase 2 is built and runs: the VFS over a host directory with `/proc` and
`/dev` beside it, the descriptor layer, the pipe, and the file syscalls
with their `*at` forms. A scripted sequence of 291 file operations traces
identically under the core and on the Rocky 8 oracle (criterion 2), and a
tree the kernel writes reads back from WSL with identical metadata and the
reverse (criterion 3). `doc/design/Core-Phase2.md` records it, with the
divergences it chose. Every other syscall number returns `-ENOSYS`, which
is what a 4.18 kernel does for what it lacks.

Nothing else of the core exists yet. Fork, `exec`, signals, threads and the
rest are designed in 0011 and unbuilt, and this document says so rather than
describing them as though they were here.

## Target and claim: x86_64-elfsysvnt-linux-gnu

The triple is `x86_64-elfsysvnt-linux-gnu`. The vendor field is what carries
the platform's identity; the `linux` field is a claim about the kernel ABI a
program compiles against, and under this design it is true everywhere. That is
an inversion of what it used to mean. The bounded claim was written when the
`syscall` instruction was the thing the platform could not honour; now the
instruction is never reached under N, where the gate takes its place, and is
the interface itself under H.

el8 source is Rocky 8.10's, acquired outside the repository rather than
vendored into it, and pinned where a spike needs a specific package.

Settled by: DR-0001, DR-0002, DR-0005.

## Toolchain and images

This section is substrate N's. N cannot trap `syscall`, so the userland above
it is rebuilt to reach the gate, and the toolchain is what rebuilds it. Under H
there is no toolchain in the path at all: the whole point of that substrate is
that shipped el8 binaries run unmodified, so every default below is a statement
about images N loads and about nothing else.

Three defaults are compiled in rather than left to a build's command line,
because a default that has to be remembered is one that will be forgotten in a
package nobody reviews.

The red zone is honoured. `-mno-red-zone` was carried for a while as insurance
against a delivery that would clobber the 128 bytes below `%rsp`, and it is
retired: delivery reserves the red zone before it builds a handler frame, which
a spike measured over two thousand deliveries, and the toolchain no longer pays
for the workaround.

Images are linked granule-separable, so a program's own protection changes land
at the granule rather than the page, and the kernel does not have to widen a
protection request to a boundary the program did not ask for.

CET is opted out in the toolchain default rather than only in the rpm macros,
so a package built by hand gets the same shape as one built by the package
tooling.

Settled by: DR-0050, DR-0061, DR-0062.

## Thread pointer and TLS

The two substrates answer this differently, and the difference is the clearest
example of why the interface exists. Under N the thread pointer is reached
through `%gs`; under H it is `%fs`, where Linux has always kept it.

**Under N**, `%fs` is not available. Windows does not preserve a user-written
`%fs` base across a context switch — the spike measured that on 2026-08-29 and
the answer was no, which took the ordinary Linux carrier off the table before
any code was written. Four `%gs` carriers were measured; three persist and
address at about five cycles, and the one in use is carrier C1: `TlsSlots[63]`
in the TEB, `%gs:0x1678`, one load. The slot is reserved from `TlsAlloc` by
setting bit 63 of the PEB's `TlsBitmap` when the substrate starts (spike
peb-tls-bitmap: seventy allocations after the set never return it, nor does a
DLL loaded later), and a substrate that finds the bit already set refuses to
start rather than share the word. The ABI above it is glibc's own: `%gs:TP`
holds the TCB pointer, `%gs:TP+8` the stack-protector canary, `%gs:TP+16` the
pointer guard, with `TP` = `0x1678`, which is what `tls.h`,
`-fstack-protector` and `PTR_MANGLE` read. The kernel sets it through the
interface's `tp_set`, and `arch_prctl(ARCH_SET_FS)` returns `EINVAL`, because
a program built for this substrate never asks.

**Under H**, the guest owns its own segment bases and
`arch_prctl(ARCH_SET_FS)` writes the vCPU's FS base. The three words sit where
glibc always put them, no carrier had to be chosen, and none of the measurement
above applies.

The consequence for images is N's alone. No image N loads may carry a
`%fs`-relative thread-pointer access; the linker emits such relocations unasked,
so the toolchain refuses them at link time rather than rewriting them at load
time, a rewriter being a heuristic and a heuristic in the TLS path failing
silently. Under H the shipped el8 binaries are full of exactly those
relocations, and that is what H is for.

The static-TLS surplus and the shape of the DTV are fixed so that a vendor
image's own TLS requirements are satisfied without renegotiation after the
fact.

Settled by: DR-0024, DR-0063, DR-0101.

## The loader

The kernel maps an image itself, under either substrate: the program's
segments, then the interpreter its `PT_INTERP` names, then the initial
stack with the auxiliary vector, and it jumps to the interpreter's entry, or
the program's when there is none (0011 § 5). `exec` has one classifier, and
an interpreter chain is followed at most four hops before it is refused,
which is a limit rather than a recursion. That is kernel behaviour, above
the substrate line, and it is the same either way.

The kernel is not a dynamic loader and has none. What runs at the
interpreter's entry is glibc's own `ld.so`, doing its work through `mmap`,
`mprotect`, `open` and `read` the way it does on Linux, reading glibc's own
`/etc/ld.so.cache`, resolving symbols by glibc's rules, and maintaining the
link map a debugger walks. Under H it is el8's `ld.so` as shipped; under N
it is the same `ld.so` rebuilt against the gate with the rest of glibc
(0011 § 16), and the kernel refuses at `exec` an image whose
`.note.elfsysvnt.abi` names a gate ABI it no longer offers. Nothing about
loading a dynamic object is this platform's to implement, and the records
that once described a loader of its own are retired.

Settled by: DR-0027, DR-0103.

## Process shape

`fork` clones the address space, and the two substrates do it in different
processes. Under N a Linux process is an NT process and `fork` is
`RtlCloneUserProcess`, the fork-shaped wrapper over `NtCreateUserProcess`,
which returns `STATUS_PROCESS_CLONED` in a child on a live thread (spike 35:
the raw null-section clone copies the address space but cannot start a
thread in it; the wrapper measured 5.1 ms on a small process). Under H every
Linux process is a page-table root in the one kernel process, and `fork` is
the kernel's own: copy the tables, clear the write bit in every eligible leaf
of both trees, count the frames, and resolve the first write on either side
when the guest's `#PF` reaches the kernel as an exception exit, with `%rip`
still at the store and no flush needed (spike 44: 0.5 to 0.75 ms for 576 MB
of tables, about 25 µs a copy-on-write fault). What crosses the fork is
enumerated rather than assumed either way, and the child checks what it
received rather than trusting that it arrived.

`vfork`, and `clone` with `CLONE_VM | CLONE_VFORK`, is what el8's glibc 2.28
`posix_spawn` issues (spike 46: `__spawnix` calls `__clone` with `0x4111`,
and `__vfork` is syscall 58). Under H it is a task on the parent's root with
the parent's tasks held until `execve` or exit, which is Linux's semantics
exactly. Under N there is no shared address space to offer, so it is a fork
plus a wait, and the `sysdeps` port patches `spawni.c` back to the pipe-based
error report and `vfork.S` to a fork; a program relying on `vfork`'s shared
memory in its own code sees a recorded divergence under N.

A signal is delivered by building a frame on the target's stack and resuming
it there. The frame is built below the red zone, so a handler that returns into
code relying on those 128 bytes finds them intact, and a thread already inside
a kernel wait takes the delivery when the wait returns rather than in the
middle of it. That holds on both delivery paths: the asynchronous one, where
the kernel builds the frame (spike 38), and the synchronous one under N,
where NT dispatches a fault on the faulting thread and places its own records
568 bytes and more below the interrupted `%rsp` (spike 47). `SIGSTOP`,
`SIGCONT` and `SIGKILL` are the process seam's: under N the supervisor
applies `NtSuspendProcess`, `NtResumeProcess` and `NtTerminateProcess` to the
process handle; under H the kernel marks the tasks, cancels those on vCPUs,
and releases the root.

A fatal signal leaves an ELF core, written by the kernel from the VMA tree and
the thread state it already holds.

Settled by: DR-0029, DR-0030, DR-0033, DR-0099.

## The address space

The kernel keeps the address space of record itself, a tree of VMAs it edits
first and asks the substrate to realise second, under both substrates. The
two substrates differ here as much as they do over the thread pointer.
`AT_PAGESZ` reports 4096 under both: it is the unit at which presence and
protection change once memory is mapped, which is what a program does
arithmetic with, and not the unit at which a reservation may start.

**Under N** the kernel reserves the user range as a placeholder in the sense
`NtAllocateVirtualMemoryEx` gives the word, and replaces pieces of it with
section views, committing lazily through a vectored handler on first touch. NT's
VADs are the mechanism and the kernel's tree is the record over them. A single
placeholder holds at half the user range; the literal whole-range claim does not
hold on this kernel, and the practical half-range one does.

The alignment invariant N builds against is the documented 64 KB —
`p_vaddr ≡ p_offset (mod 64 KB)` — even though this host accepts a 4 KB split
and a 4 KB file-backed view. Building against the coarser rule and probing for
the finer one costs little and does not rest a shipped artifact on one
machine's measurement.

**Under H** none of that applies. The kernel writes the guest page tables, so a
VMA is realised at page granularity by writing entries, a file mapping at any
alignment is a question of which host pages back which guest pages, and there
is no arena because there is no NT VAD in the guest's way. Guest physical
memory is the kernel process's own address space mapped into the partition,
identity, and the mapping is lazy and pins nothing (spike 42): a reserved
host range sits behind a mapping until the guest touches it, the touch
arrives as a memory-access exit that says the GPA is mapped and the host page
absent, and the kernel commits there. That exit is the lazy-commit handler
one level down. A first touch costs about 13 µs against 4 µs for a resident
page, so the kernel commits and populates in 2 MB chunks around a fault
rather than page by page; `MADV_DONTNEED` is a decommit, and the guest's next
touch is the same exit.

The veneer's address-space protocol, a low window a parent reserved for a
suspended child and mapped through Cygwin's `mmap`, is history
(`doc/history/veneer-address-space.md`) and none of its records govern here.

Settled by: DR-0014, DR-0098, DR-0100.

## Verification

`doc/design/Verification-Plan.md` carries the bar per conformance class, the
gate, and the substitution ledger. Two things about it belong here because they
shape the architecture rather than only checking it.

The substrate is certified against a bar written before it was implemented, per
call, and the suite runs against a mock as well as against N — so the contract
is held honest from both sides, and a substrate cannot pass by being the only
thing the tests have ever seen.

A spike reproduces its findings, not its measurements. Verdicts and case words
must come back identical; cycle counts, addresses and timings are free to move.
`test/spike-regen.tsv` is the registry and `test/t3-regen.sh` is what reruns
them, which is why a phase-0 measurement is still a claim this tree can defend
rather than a number in a document.

## Not verified

The spikes are single-host, single-Windows-build, single-AMD-part
characterisations. None is a portability claim, and the floor the design states
— Windows 10 1809 for function, Windows 11 for certification — is a floor
derived from when each API arrived, not from a run on each.

Substrate N is certified against its conformance bar and has now run one
program end to end. It has not run a real el8 binary, a dynamic image, a second
thread, or a signal.

Substrate H is unbuilt. Its nine calls are each backed by a spike, including
the interrupt call that the two-substrate bet depends on, and its shape (one
kernel process, one partition, a root per Linux process) is backed by spikes
42 to 45, but no core has sat on it. The pool of vCPUs under many threads,
the lazy mapping at 64 GB and under a working-set trim, and the
copy-on-write fault from ring 3 were each measured on 2026-09-06 (spikes 43,
42 and 44 extended) and hold; a host short enough of memory to page guest
pages to disk under load was not produced, and a full register-file switch
was not timed.

The process seam is an inventory restated from 0011's supervisor, not a
built interface; phase 3 will find what it misses. The keyed-event protocol
for process-shared futexes under N is a design; criterion 12 under contention
is its measurement. Timed futex waits under N wake on the clock interrupt:
a tick late at the 15.6 ms default, under half a millisecond at the 0.5 ms
resolution the kernel sets at start, and sometimes early, so the wait loop
re-reads the clock before reporting `ETIMEDOUT` (spike 50). The power cost
of the raised resolution is not measured.

Everything in "The loader" and "Process shape" above is designed and recorded,
and none of it is built. The restart stub under "The gate" is a design too;
Phase 1 delivers no signals. Those sections describe what the records settle,
which is not the same as describing what runs. The distinction is the one this
document exists to keep, and it is easier to lose here than anywhere else in
the tree.
