# Proposal 0012 — the process seam, the shape of substrate H, and what 0011 left unsaid

Status: accepted
Author: drafted 2026-09-05 for Philip Dye, under the operator's grant to
proceed after the design review of 0011 and the topology note that followed it
Date: 2026-09-05
Analysed against: `7286acc` on `wp/0012-h-topology` (main at `5f4be26`);
`libc-2.28-251.el8_10.40` for the glibc facts; Windows `10.0.26200.9168` on
`ins-15` for every measurement
Classification: expensive to undo — the process model under H, the contract
the shipped userland holds against `vfork`, the layering the core is written
to, and the retirement of four decision records. Full proposal; records owed.

Proposal 0011 designed a Linux kernel personality with one core over two
substrates and said, in one sentence, that under the hypervisor substrate
"every one of those paragraphs is simpler". A design review of 0011 read
that sentence against the mechanisms and found it describes a kernel that
lives inside the guest, which this kernel does not; that the interface's
prose names no address-space object; that nothing says how I/O reaches user
memory; that `vfork` is specified against a glibc that does not exist; and
that several claims about N rested on paths nobody had measured. Six spikes
then measured what could be measured, rows 42 to 47 of `doc/milestones.md`.
This proposal is what the design is once those results are in: it completes
0011 rather than replacing it, and where it contradicts 0011 it says so by
section.

## Context and scope

The review is a working note under `a/` and is not cited from here; its
findings are restated where they bear. What is tracked is the evidence:
`spike/whp-gpa-map/`, `spike/whp-partition-cost/`, `spike/whp-pagetable-fork/`
and `spike/whp-clone-host/` for the shape of H; `spike/glibc-spawn-clone-flags/`
for what el8's libc asks of `clone`; `spike/redzone-sync-fault/` for what NT's
exception dispatch does to a leaf's red zone. Each is registered in
`test/spike-regen.tsv` and reproduces.

In scope: the layering between the core and its substrates; the topology of
substrate H and the mechanisms that follow from it (memory, fork, exec,
signals, the host process); the address-space object in the substrate
interface; how the kernel's I/O reaches user memory; `vfork` and `CLONE_VM`
under each substrate; syscall restart under N's gate; process-shared futexes
under N; the standing of DR-0050 for synchronous faults; the retirement of
the veneer's address-space records; and the two entry documents (`README.md`,
`AGENTS.md`) that still describe the veneer.

Out of scope: choosing N or H for any deployment (both remain, for different
clients, and the client's environment decides); the dynamic-translation
substrate 0011 names; the audit of `Requirements.md`, `Verification-Plan.md`,
`target-definition.md` and `doc/ROADMAP.md` for pre-0011 text, which is owed
and is named in the closing summary rather than done here; anything in
0011's phases 2 to 9 beyond what this proposal changes about them.

## Goals and non-goals

Goals. A reader implementing substrate H can say, for fork, exec, a page
fault, a signal and a syscall, which process the code runs in and what it
touches, without inventing any of it. The core's layering names the seam
between what is per process and what crosses processes, so the VFS and the
supervisor are written against an interface with two realisations rather
than against ALPC alone. `posix_spawn` from an unmodified el8 glibc works
under H and is patched knowingly under N. The nine-call interface keeps its
nine calls and its C form, and its prose says what its C form already does.
Every claim this proposal makes about the host is one a registered spike
measured, or is marked as a reading.

Non-goals, each of which could reasonably have been a goal.

Building substrate H. This designs it; 0011 § 18's phase plan still puts N's
core first, and the operator's steer in `doc/design/Core-Phase1.md` D0 stands.

Deciding the TLS carrier under N. 0011 § 6 and DR-0003 name different
carriers; `AGENTS.md` reserves the TLS model to the operator. The conflict is
stated, with a recommendation, in the open questions.

Amending verification criteria 3 and 4. Both were shown unmeetable as
written (spike 39; spike 35 with spike 44). Changing what the project
promises to verify is the operator's call, and both are open questions here.

## The design

Subsections are cited as 0012 § N from other documents.

### 1. Three layers, not two

0011 § 3 draws one line, the substrate interface, and puts "everything from
the syscall table down to the point where user code has to be run" above it.
That is right about what is written once, and it hides a second line that
0011 § 2 and § 5 draw in prose without naming: the boundary between what one
Linux process owns and what crosses processes. 0011 puts the second kind in a
supervisor, `lk-init.exe`, reached over ALPC, because under N a Linux process
is an NT process and nothing else can hold the pid table. Under H, as § 2
below establishes, every Linux process lives in one kernel process, and the
same state is a table in that process's heap.

So the core has two seams beneath it, on different axes:

| Layer | What it holds | N | H |
|---|---|---|---|
| Core | syscall table, VFS, descriptor layer, signal semantics, VMA tree, process and thread model | one source | the same source |
| Process seam | pid allocation and parent links; process groups and sessions; wait queues and exit status; signal send to another process; ttys, ptys and the line discipline; the `AF_UNIX` rendezvous namespace; SysV and POSIX IPC namespaces; advisory locks; the inotify watch registry; process-shared futex queues; `SIGKILL`, `SIGSTOP` and `SIGCONT` as applied to a process that may not be responding | `lk-init.exe` over ALPC, the shared page, and named sections (0011 § 2, § 5, § 7, § 12, unchanged) | function calls in `lk-kernel.exe` |
| Substrate | the nine calls of `Substrate-Interface.md` | NT threads and the gate | WHP vCPUs and the shim |

The seam is an interface the core calls, with its realisation chosen with
the substrate at build time. Under N its realisation is the supervisor 0011
already designed; nothing in those sections changes except that they now
describe one realisation of a named seam. Under H the realisation is direct.
The fast paths 0011 deliberately kept out of the supervisor (ring pipes in
shared sections, signal ports, `RtlWaitOnAddress` futexes) are core
mechanisms that work in either realisation and stay as designed; under H
several of them collapse to simpler forms and the core is free to take the
simpler one where the seam offers it.

Two consequences follow. The VFS, the supervisor and the signal code from
0011 phase 2 onward are written against the seam, never against ALPC by
name, in the same way the core is written against the nine calls and never
against `Nt*`; `bin/check-substrate-line` gains the ALPC and named-object
symbols to its forbidden list for `core/`. And the process-transport
realisation under N is the first one built, since N's core is first; H's is
a second implementation of the same interface, as N and H are of the nine
calls.

### 2. Substrate H: one kernel process, one partition, a page-table root per Linux process

Substrate H is `lk-kernel.exe`, one per Windows user per installation root:
a Win32 process, because `WinHvPlatform.dll` imports `kernel32`, and there
is therefore no ntdll-only host under H. It holds one WHP partition. The
measurement that fixes this shape is spike 43: on this build a process may
set up several partitions but only one at a time may hold guest-physical
mappings (`WHvMapGpaRange` into a second is refused with `0xC0370008` until
the first unmaps), so a partition per Linux process inside one kernel process
is not available, and a partition per Linux process in a host process per
Linux process is the shape § 8 below records as measured and not taken.

A Linux process is a page-table root in guest physical memory plus the
kernel's own structures for it, in the kernel process's heap. A Linux thread
is a task. The partition carries a pool of vCPUs, sized to a small multiple of
the host's processor count and bounded by the 240 spike 43 created; a task
runs on a vCPU while in user mode and gives it up on a syscall exit, so the
number of Linux threads is not bounded by the pool. Every task also owns an
NT thread, which drives its vCPU while the task is in user mode and blocks in
NT waits on the task's behalf while it is in the kernel; spike 43 q5 measured
a vCPU driven by one thread and then another with its state intact. A
running task is interrupted with `WHvCancelRunVirtualProcessor` (spike 41); a
task not on a vCPU is interrupted by marking it, which the contract's "soon"
allows.

Guest physical memory is the kernel process's own virtual address space,
mapped into the partition with `WHvMapGpaRange` range by range as VMAs are
realised, identity: a guest physical address is the host address of the
byte. `user_copy_*` under H is a walk of the process's own page tables, which
the kernel wrote, to the host address, and a copy. Spike 42 fixes how memory
behaves behind that mapping. Mapping is lazy and pins nothing; a reserved
host range may sit behind a mapping, and a guest touch of it arrives as a
memory-access exit that says the GPA is mapped and the host page absent,
which is the lazy-commit handler "one level down" that 0011 § 4 promised,
and it is distinguishable at the exit from a touch outside any mapping. So
an anonymous VMA is a reserved host range mapped at creation and committed
on the exit; a first touch costs about 13 µs against 4 µs for a resident
page, so the kernel commits and populates in 2 MB chunks around the fault
(`WHvAdviseGpaRange` populate: 64 MB in 6 ms, after which a touch costs what
a resident page does) rather than page by page. `MADV_DONTNEED` is a
decommit, which spike 42 q5 shows the guest sees as the same exit on its next
touch, and a recommit reads zero. A file-backed VMA is an NT section view in
the kernel process, mapped into guest physical memory, and reached at 4 KB
granularity through the process's page tables whatever the view's alignment;
`MAP_SHARED` between two Linux processes is the same host pages in two roots.
The 64 KB rule 0011 § 4 imposes under N does not exist under H.

`fork` is what 0011 § 4 said and spike 44 measured: copy the page-table
tree, clear the write bit in every copy-on-write-eligible leaf in both trees,
bump each frame's count, and resolve the first write on either side at the
fault. The fault reaches the kernel because the partition is created with
`ExtendedVmExits.ExceptionExit` and a bitmap that routes `#PF` (and `#GP`,
`#UD`, `#DE`, `#DB`, `#BP`, for the signals they become) to the host as
`WHvRunVpExitReasonException`; the exit carries the fault address in
`ExceptionParameter` (the CR2 register reads zero at that exit and is not
used), the error code, and `%rip` still at the faulting instruction, so the
kernel fixes the leaf and runs again with no flush: spike 44 q4b shows the
stale translation is not held. The copy costs what the tables cost, about
0.1 ms for 64 MB and 0.5 to 0.75 ms for 576 MB, and a copy-on-write fault
costs about 25 µs over the same store without one. That second number is the
one to design around: a child that touches a thousand pages before `exec`
pays 25 ms, so the kernel copies eagerly at fork what the child will
certainly touch (the top of its stack, the page holding the fork return
path), and `vfork` (§ 5) is the answer for the rest.

`execve` is a new root over the same task, the old root released. Frames are
reference counted and freed at zero; the kernel process's own address space
is the frame allocator's arena, host-reserved and committed as § 2's memory
rules say.

The guest side is 0011 § 3's ring-0 shim, narrowed: it owns `LSTAR`, `STAR`
and the GDT, and its `syscall` entry spills registers to the per-vCPU page
and executes `hlt`, which is the exit the host reads (spike 40, `halt-in-shim`,
a median 5.0 µs round trip). It owns no IDT, because every exception the
kernel cares about exits to the host before delivery. The shim and its tables
are mapped into every root at a fixed supervisor-only range above the user
half, exactly as a kernel's text is.

Signals under H are the core's signal semantics with the seam's in-process
realisation. Delivery to a running task is a cancel; the frame is written by
the kernel through the process's page tables at the address `Substrate-
Interface.md` prescribes, 128 bytes below the interrupted `%rsp` (spike 41 q4
delivers exactly that); `SIGSTOP` marks the process's tasks stopped and
cancels those on vCPUs, and `SIGKILL` releases the root, so the
`NtSuspendProcess` and `NtTerminateProcess` of 0011 § 5 are N's realisation
and not the seam's contract. Descriptor passing, `ptrace`, `/proc/<pid>/mem`
and process-shared futexes are reads and writes of structures in one heap.

What H costs, stated so nobody mistakes it: a syscall is about 5 µs (spike
40) against about 1 µs under N; a first touch of fresh memory is 13 µs unless
populated ahead; a copy-on-write fault is about 25 µs against N's NT-side
copy; every Linux process of a user shares one kernel process's fate, which
0011 already accepted for a supervisor crash and which is the argument for
Rust that 0011 open question 4 makes. What H buys is unchanged from 0011: a
real FS base, a real `syscall`, page-precise mappings, el8's own RPMs, and,
new since the measurements, a fork in well under a millisecond at hundreds of
megabytes, no process clone, no thread hijack, and no anonymous executable
memory in the host.

Under H there is no `lk-host.exe`; `lk-init.exe` and `lk-kernel.exe` are one
process; `lk-term.exe` and `lk.exe` are as 0011 § 2 has them.

### 3. The interface's object was always there

`Substrate-Interface.md` says the core "names a thread by its Linux tid and
a range by its address, and never learns which substrate is under it", and
gives `as_clone() -> child` with no receiver. The C form in
`substrate/substrate.h` says something more precise, and it is the C form
the mock and N were certified against: every call takes a `struct substrate
*s`, `as_clone` returns a new `struct substrate` for the child, and
`substrate_create()` yields a fresh one. A `struct substrate` is one address
space and the threads that run in it. That is the object the review found
missing from the prose. It is not missing from the interface.

This proposal amends the prose to match: one `struct substrate` per Linux
process, created by `substrate_create` at `fork` (through `as_clone`) or at
the first `exec`; a substrate's threads are the tids the core has started in
it; `as_clone` under H is the page-table copy of § 2, resolved on exception
exits, and under N is `RtlCloneUserProcess` as before; the partition, under
H, is state shared by every instance the kernel process holds, and no call
names it. The signatures do not change, the count stays at nine, and the
conformance suite runs unchanged; criterion 2 below checks that.

### 4. How I/O reaches user memory

The rule: the kernel never hands a host call a user address. Every byte that
moves between user memory and a host object (`NtReadFile`, `NtWriteFile`,
AFD, a section) goes through `user_copy_in` or `user_copy_out` and a kernel
buffer, in chunks. Phase 1's `write` does this already (`core/syscall.c`, a
4 KB bounce), and it is the pattern.

Two facts make the rule mandatory rather than tidy. Under N, a user buffer in
a lazily committed range fails inside the host call: the I/O manager probes
the buffer in kernel mode, the vectored handler never runs, and the call
returns `STATUS_ACCESS_VIOLATION`. Under H a user address is a guest virtual
address that no host call can take at all, and the host pages behind a range
need not be contiguous. gVisor avoids both by mirroring every application
mapping into the sentry at the same address; this design has no mirror and
takes the copy. The cost is one `memcpy` per I/O, which is what the veneer
arc paid at every crossing and what this design's 1 µs and 5 µs syscall
floors dwarf for any I/O that reaches a device.

The witness: the host glue's API (`core/host.h` and its successors) takes
kernel pointers and lengths and never a `uint64_t` user address, and
`Architecture.md` states the rule. `bin/check-substrate-line` continues to
refuse a raw user-pointer dereference in `core/`; a host call fed a user
address is a review item, not a mechanical one, and is named as such.

### 5. `vfork`, `CLONE_VM`, and what glibc 2.28 actually asks for

0011 § 6 says `clone` "with `CLONE_VFORK` but without `CLONE_VM` is the
`vfork` glibc's `posix_spawn` uses", implements it as fork plus wait, and
returns `EINVAL` for every other combination. Spike 46 read the shipped
`libc-2.28.so`: `posix_spawn` and `posix_spawnp` reach `__spawnix`, which
calls `__clone` with `0x4111`, `CLONE_VM | CLONE_VFORK | SIGCHLD`, the child
running `__spawni_child` on the parent's memory and reporting an `exec`
failure by writing into the parent's stack; and `__vfork` is syscall 58.
0011's sentence is wrong, and as written the design returns `EINVAL` to the
one `clone` shape `posix_spawn` makes.

Under H: `vfork` (58) and `clone` with `CLONE_VM | CLONE_VFORK` create a task
on the parent's root; the parent's tasks are held until the child calls
`execve` or exits, as Linux holds them; `execve` gives the child its own root
and releases the parent. This is exact Linux semantics and costs nothing § 2
did not already build. `clone` with `CLONE_VM` and neither `CLONE_VFORK` nor
`CLONE_THREAD` is `EINVAL`, as 0011 had it; nothing in the acceptance set
issues it.

Under N: there is one NT process per Linux process and no way to run a child
on the parent's memory, so `vfork` and `clone(CLONE_VM | CLONE_VFORK)` are
fork plus a wait on the child's `exec` or exit, and the `sysdeps` port
patches two files 0011 § 16 did not list: `sysdeps/unix/sysv/linux/spawni.c`
reverts to the pipe-based error report glibc used before 2.24, and
`sysdeps/unix/sysv/linux/x86_64/vfork.S` becomes a fork. A program that
relies on `vfork`'s shared memory in its own code, outside glibc, sees a
recorded divergence under N.

### 6. Syscall restart under the gate

Linux restarts an interrupted syscall by backing `%rip` up two bytes over
the `syscall` instruction and restoring `%rax` from `orig_rax`. Under N there
is no instruction to back over: the gate is reached by a `call` of unknown
length, and re-executing a `call` would push a second return address. The
mechanism is a vDSO stub, `__lk_restart: jmp *_lk_gate(%rip)`. When the gate's
exit path finds `-ERESTARTSYS` (or its siblings) with a handler to run and
`SA_RESTART` set, it builds the signal frame with `%rip` at the stub, `%rsp`
at the gate-entry value (still pointing at the original return address) and
`%rax` reset from `orig_rax`, which the gate saved at entry; the handler
returns through `rt_sigreturn` into the stub, the stub jumps into the gate
with the stack exactly as the original `call` left it, and the syscall runs
again. `-ERESTART_RESTARTBLOCK` uses the same stub with `%rax` set to
`restart_syscall`. Under H the instruction is real and the two-byte rewind is
Linux's. `ptrace` reads `orig_rax` from the gate's saved word under N and
from the shim's spill under H; `seccomp`'s `si_call_addr` under N is the
return address, since the call's length is unknown.

### 7. The red zone on a synchronous fault

The review argued that NT's exception dispatch, built for an ABI with no red
zone, would write its records directly below the faulting `%rsp` and corrupt
a leaf's temporaries before N's vectored handler ran, and that DR-0050 would
need reversing for N. Spike 47 measured it: over a thousand lazy-commit
faults and a thousand `int3`s, nothing below `%rsp` changed; NT places the
`EXCEPTION_RECORD` 568 bytes and the `CONTEXT` 1832 bytes below the
interrupted pointer, and a control in which the handler itself clobbers
`rsp-8` is caught every time. DR-0050 stands for both delivery paths. N's
lazy commit through the vectored handler, and synchronous signals taken
there, need no toolchain change.

### 8. Shape A, measured and not taken

The other topology for H, one host process per Linux process each holding a
partition, was measured before it was declined. Spike 45 clones a Win32
process holding a partition: the child runs, does heap, event, wait,
`VirtualAlloc` and TLS work, cannot `LoadLibrary`, and builds a partition of
its own to a first exit in 1.3 to 2.3 ms; a run on the inherited partition
handle hangs if it is the child's first WHP call and drives the parent's
guest if it is not. The clone itself costs 3 to 5 ms with 4 MB mapped and 6
to 8 ms with 256 MB touched, the same with that memory unmapped, so the
hypervisor adds nothing to the clone and the child's remap of its guest
memory, unmeasured, is what would scale with the address space. Shape A is
available. Its fork is the NT clone plus 2 ms of partition plus a remap,
against § 2's sub-millisecond table copy; its `vfork` has no mechanism;
its `ptrace` crosses two address-space translations; it keeps every
cross-process protocol of the N realisation. Nothing a client needs selects
it. It is recorded here so that nobody reopens it without a new fact.

### 9. Process-shared futexes and timeouts under N

`RtlWaitOnAddress` is per process (spike 36 q4). Under H the question does
not arise. Under N a futex word in `MAP_SHARED` memory is served by an NT
keyed event, one per installation root, with the key derived from the
backing's device, inode and offset, and a waiter count per key kept in the
supervisor's shared page and updated with interlocked operations before the
wait and after the wake; a waker releases with an infinite timeout when the
count says a waiter is registered and with a zero timeout otherwise, which is
how the pre-Vista critical section closed the same lost-wake race. The
supervisor is not on the path. Criterion 12 (PostgreSQL 10, whose
`sem_init(pshared)` semaphores are exactly these words) runs under
contention, not once. Futex timeouts under N wake on the system clock
interrupt; the kernel raises the resolution with `NtSetTimerResolution` at
start and the power cost is recorded when measured.

### 10. Records and documents this reopens

DR-0008, DR-0028, DR-0064 and DR-0077 describe the veneer's address space:
Cygwin's `mmap` as the mapping primitive, a low window reserved by a parent
into a suspended child, granule-only protection precision through a
forwarding thunk, and the plain-PE reconcile of that window. By DR-0097's
own test, no governing document can state in the present tense a thing they
settle: the subject is absent. They retire under DR-0100, as the eleven did
under DR-0097, unedited. DR-0014 (`AT_PAGESZ` reports 4096) survives, is
true of 0011 § 4 under both substrates, and is rehomed in `Architecture.md`
§ The address space. `doc/design/Address-Space.md`, which carries the
veneer's protocol at length and is where `Architecture.md` sends a reader for
detail, moves to `doc/history/veneer-address-space.md`; the address space of
record is 0011 § 4 as `Architecture.md` states it, with spike 37's bounds.

`README.md` and `AGENTS.md` still open with the veneer ("a Cygwin-derived
userland kernel", "this project re-faces that DLL"), list sub-documents this
repository does not hold, and reserve decisions in terms of work packages
that went to the veneer sibling. Both are rewritten for the design of record
in this change, `AGENTS.md` under the operator's agent-instructions
standard, and both keep what is true of el8 and this host rather than of the
veneer.

## Alternatives considered

Shape A for H. Measured (spike 45) and viable; declined in § 8 for fork cost,
the absence of a `vfork` mechanism, and for keeping every cross-process
protocol that H's whole advantage is to remove. Recorded, not abandoned: a
client requirement that Windows see a process per Linux process would reopen
it.

Shape B1, a partition per Linux process inside one kernel process. Not
available on this build (spike 43); one mapped partition per process.

Dropping H, the review's first recommendation before the measurements. The
four measurements that could have sunk it came back clean or better, and the
fork number came back where shape B's arithmetic said it would. H stays,
designed and unbuilt, for the client the topology note describes: a
hypervisor present and stock el8 binaries required.

An address-space handle added to every `as_*` call. The C form already
carries the object as `struct substrate`; adding an argument would change
nine signatures and the certified suite to state what `self` states. § 3
amends the prose instead.

A host-side mirror of user memory for zero-copy I/O, gVisor's shape. Under N
it needs every I/O's pages committed before the call (a probe in the I/O
manager cannot take the vectored handler); under H it is a second mapping of
every VMA. The copy is cheaper than either and is already the Phase 1
pattern.

`vfork` as fork plus wait under H. Spike 46: `posix_spawn` would get `EINVAL`
or a child whose error report writes into memory the parent cannot see. Under
H there is no reason to pay that; the parent's root is right there.

Reinstating `-mno-red-zone` for N, or committing all anonymous memory
eagerly. Both were the remedies if spike 47 had gone the other way. It did
not.

A tenth call, or a second interface, for the process seam. The seam is a
core-internal interface with two realisations, not a substrate call: the
substrate runs user code, the seam owns cross-process state, and neither
needs the other's vocabulary. Kept separate on purpose so the substrate
interface stays at nine.

## Cross-cutting concerns

Compatibility with what exists. `substrate/substrate.h` is unchanged;
`substrate/run.sh` against the mock and against N passes as before;
`core/run.sh` prints `hello` as before. Nothing built under 0011 phase 1
changes. The six spikes of this change are additive.

Documents. `Architecture.md` gains the three-layer statement and the process
seam, H's shape in § The shape of the system and § Process shape, the I/O
rule, the `vfork` contract per substrate, the restart stub under § The gate,
DR-0014's new home, and loses its pointer to `Address-Space.md`.
`Substrate-Interface.md` gains § 3's statement of the object and corrects
`as_clone`'s H realisation. `README.md` and `AGENTS.md` are rewritten.
`Address-Space.md` moves to history with a heading saying what it was.

Records. DR-0098 ratifies this proposal and settles the three layers, H's
shape, the object, and the I/O rule. DR-0099 settles the `vfork` and
`CLONE_VM` contract per substrate. DR-0100 retires DR-0008, DR-0028, DR-0064
and DR-0077 and rehomes DR-0014. Each carries `Amends:`.
`bin/check-design-links` must exit 0 with every retired record marked in the
index and every in-force record homed.

Migration and rollback. Nothing persisted changes. Reversing any decision
here is a new record pointing back.

Security surface. Unchanged from 0011. Under H the kernel process is the one
Windows process a user's Linux processes share; it runs as that user.

Idempotency. No installer or configurator is touched.

## Verification criteria

1. `bin/check-design-links`, `bin/check-doc-refs`, `bin/check-worknote-refs`
   and `bin/check-suites` exit 0 at every commit of this change (gate).
2. `substrate/run.sh` and `substrate/run.sh --substrate n` exit 0 with
   `substrate/substrate.h` byte-identical to `5f4be26`'s (report; the
   interface's C form is unchanged by § 3).
3. `core/run.sh` exits 0: the Phase 1 program prints `hello` against the
   oracle (report).
4. `test/t3-regen.sh whp-gpa-map whp-partition-cost whp-pagetable-fork
   whp-clone-host glibc-spawn-clone-flags redzone-sync-fault` reports six
   regenerated and none rotted (report).
5. `doc/design/Architecture.md`: every section that describes a mechanism
   names N, names H, or says both substrates answer alike; the sections are
   listed in D9 below and each is read (review).
6. `grep -c 'Cygwin-derived\|re-faces' README.md AGENTS.md` is 0, and neither
   file names a `doc/design/` path that does not exist (gate, by
   `check-doc-refs`).
7. `git show main:doc/design/Address-Space.md` fails and
   `doc/history/veneer-address-space.md` exists with a heading that says what
   it was (report).

## Open questions

Each is the operator's: a reserved decision, a change to a stated promise,
or a value the ladder cannot reach (tier 8). Each carries its survivors and
a recommendation, and none is settled by this proposal.

1. The TLS carrier under N. 0011 § 6 states a single-load ABI (`%gs:TP`,
   `%gs:TP+8`, `%gs:TP+16`), which is carrier C1 or C4 of spike 6; DR-0003
   chose C3, two loads through `NtTib.StackBase`, with no fixed `%gs` offset
   for GCC's `-mstack-protector-guard-reg=gs`. Survivors: C1 with bit 63 of
   the PEB's `TlsBitmap` reserved at process start, which removes the
   injected-DLL hazard DR-0003 cites rather than reducing it; or C3 with
   § 6's ABI rewritten to two loads and the stack protector on the global
   guard. Recommendation: C1 with the reserved bit. Reserved to the operator
   by `AGENTS.md`.
2. Criterion 3. Spike 39: Cygwin 3.6.10 does not read the LX metadata, so
   the criterion as written cannot pass. Survivors: narrow to WSL; keep
   Cygwin as a recorded divergence; drop. Recommendation: narrow to WSL.
3. Criterion 4. Spike 35 with spike 44: N's fork is a 5 ms clone and cannot
   meet 2 ms; H's table copy can. Survivors: per-substrate criteria (2 ms
   under H, the clone's number recorded beside Cygwin's under N); one
   criterion at N's number; drop the threshold. Recommendation: per
   substrate.
4. 64 KB against 4 KB under N, 0011 D4. Still parked; spike 37 on the
   version floor is the measurement that would move it.
5. The hypervisor as a prerequisite, 0011 D5. Still the operator's; this
   proposal assumes both substrates exist because different clients need
   them, and § 2 designs H on that assumption.
6. Whether `bin/check-substrate-line` should grow the ALPC and named-object
   symbols for `core/` now or when phase 3 starts. Recommendation: now, in
   the fold-back commit, so the seam has teeth before anything crosses it.

## Not verified

That the vCPU pool hands off correctly over many threads. Spike 43 measured
two threads over one vCPU; the pool is a design from that measurement.
(Measured 2026-09-06, spike 43 q7: it does, see the addendum.)

That `WHvMapGpaRange` stays lazy at tens of gigabytes and under memory
pressure. Spike 42 measured one gigabyte on an idle host. (Measured
2026-09-06, spike 42 q8 and q9: lazy at 64 GB, and trimmed pages return;
a host paging to disk under load was not produced.)

That the shim's exception-exit bitmap covers every vector a signal needs;
the bitmap this host reports is `0xf7dfb`, which includes vectors 0 through
19 except 2, 9 and 15, and `#PF` was the one exercised.

That a ring-3 store through a user page under H faults exactly as spike 44's
ring-0 store did. The mechanism is the same; the privilege level was not
varied. (Measured 2026-09-06, spike 44 q7: it does, error code `0x7` at
CPL 3, same cost.)

That the process seam's operation list is complete. It is 0011's supervisor
inventory restated; phase 3 will find what it missed.

The keyed-event futex protocol of § 9 is a design, not a measurement; its
measurement is criterion 12 under contention.

## Decision log

Each entry names the tier of the decision ladder that settled it.

D1. Whether to measure before proposing. Tier 1, by the ladder's own rule
that a fact fork is measured before any tier reads it. Five measurements the
topology note listed and two the review raised from memory were fact forks;
all seven were taken (six spikes) before a line of this proposal was
written. Two of the seven refuted the review's reading (spike 47 on the red
zone; the interface object in § 3 was found in the C form, not measured).

D2. H's topology. Tier 1 discriminated after measurement: shape B1 cannot be
made correct on this build (spike 43); shape A and shape B2 both can. Tier 2
then discriminated on fork: B2's table copy is sub-millisecond at 576 MB and
A's is the NT clone plus a partition plus a remap (spikes 44, 45), and A's
`vfork` has no mechanism (spike 46). B2.

D3. Whether the substrate interface changes. Tier 1: the C form already
carries the address-space object; a change would restate it. No change;
prose amended.

D4. Bounce buffers for I/O. Tier 1: under N a lazily committed user buffer
fails the I/O manager's probe, under H no host call can take a guest
address. The one candidate left correct is the copy.

D5. `vfork` under H as a task on the parent's root. Tier 1 after spike 46:
fork plus wait returns `EINVAL` to `posix_spawn` or breaks its error report.

D6. `vfork` under N as fork plus wait with the `sysdeps` patch. Tier 1: N has
no shared address space to offer; tier 5 (diagnosability) picked the
pre-2.24 pipe report over a silent divergence.

D7. DR-0050 stands. Tier 1 after spike 47: the fork the review raised was a
fact and the fact went the other way.

D8. The restart stub. Tier 1 among the candidates (rewind an unknown-length
`call`, re-execute the `call`, a stub the frame names): only the stub is
correct.

D9. Which `Architecture.md` sections are read for criterion 5: The shape of
the system; The gate, and what has run; Toolchain and images; Thread pointer
and TLS; The loader; Process shape; The address space. Tier 7: the sections
that describe a mechanism.

D10. Retiring DR-0008, DR-0028, DR-0064 and DR-0077. Tier 1, DR-0097's test:
no governing document can state their subject in the present tense.

D11. Rewriting `README.md` and `AGENTS.md` in this change rather than a
follow-up. Tier 5: an entry document that describes a different design is
the first thing a reader is misled by, and the operator asked.

D12. The TLS carrier, criteria 3 and 4, 64 KB against 4 KB, the hypervisor
prerequisite: tier 8, reserved or promise-changing. Parked as open questions
1 to 5 with survivors and recommendations; not settled.

D13. Three records rather than one. Tier 5: the `vfork` contract and the
address-space retirement are the two settlements a later reader will look
for on their own, and each gets an `Amends:` line that names one section.

## Addendum, 2026-09-06

The operator answered the open questions on 2026-09-05. Open question 1 is
settled by DR-0101 (carrier C1, `TlsSlots[63]`, reserved through the PEB
bitmap; spike 48 is the fact it rests on, and substrate N was re-certified
9/9 against it). Open questions 2, 3, 5 and 6 are settled by DR-0102. Open
question 4 is deferred until spike 37 runs on the version floor.

Three of the cheap measurements the decisions asked for ran on 2026-09-06.
Spike 49 confirms § 4's N-side premise: `ReadFile` into a reserved or
half-decommitted buffer fails with `ERROR_NOACCESS` and the vectored handler
never runs. Spike 50 measures § 9's timeouts: `RtlWaitOnAddress` returns a
tick late at the default clock, under half a millisecond once the kernel
sets the 0.5 ms resolution, and early in about one wait in eight, so the
futex loop re-reads the clock before `ETIMEDOUT`. Spike 39's q9 extension
finds that a POSIX rename replaces an open target and a POSIX delete of a
mapped file succeeds, while truncating below a live view is refused and a
directory cannot be renamed over an open child; the last two are recorded
divergences for the VFS. The raw-syscall census (spike 51) is the fourth
and reports when the run over every el8 package completes.

The three H measurements this proposal listed as not verified ran the same
day. Spike 44 q7: the copy-on-write fault from ring 3 is the ring-0 fault
with the user bit, 24 µs a round trip, every frame isolated, and a ring-3
store to a supervisor page refused. Spike 43 q7: 8 vCPUs under 256 threads
ran 256,000 rounds with no thread ever seeing another's registers; a
borrow-load-run-save-return round is 13 µs uncontended and the pool holds
about 260,000 rounds a second oversubscribed 32 to 1. Spike 42 q8 and q9:
64 GB maps lazily in about a second at 16 ms per gigabyte, the working set
unmoved; after the host empties the working set every guest-written page
reads back correct at first-touch cost, so the host pages guest memory as
it pages anything and the guest never sees it. One map call failed once
with `ERROR_NO_SYSTEM_RESOURCES` while the probe was being written and did
not recur under retry counting; the kernel retries that status.

Spike 51, the raw-syscall census, measures § 8's claim that the userland N
cannot take as shipped is a list rather than a fraction. Over 3780 of the
4855 packages in the worklist it is a list: 71 packages carry a `syscall`
instruction the disassembler confirms, 69 of them outside glibc, 19 with a
Go runtime, and they are runtimes and toolchains rather than anything in
the base userland. The byte scan alone reports 1419, twenty times as many,
so the disassembly step is what makes the claim measurable at all. The
count is a floor at 78% coverage; the shape is the finding.
