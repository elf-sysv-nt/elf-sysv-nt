# Substrate H: one host process per Linux process, or one kernel process for all

History, not design. Written 2026-09-05 at main `8a6a53b`, elaborating risk 1
of `doc/history/reviews/0011-design-review.md` at the operator's request; its
"Measured, 2026-09-05" section carries the results, and proposal 0012 § 2 and
§ 8 are what was decided from them. The note weighs the two topologies H
could take, what each costs, and whether a deployment could need both; it
stopped short of a proposal, and 0012 is the proposal.

The one-paragraph version. Shape A keeps 0011's documents and pays for it in
the system: every fork is an NT process clone plus a hypervisor partition
built from scratch, the host has to be a Win32 process to load
`WinHvPlatform.dll`, and `ptrace` crosses two address-space translations.
Shape B is the shape gVisor's KVM platform takes (its ptrace platform is
closer to A, so the precedent cuts both ways), and it makes fork, vfork,
futexes, signals and ptrace ordinary in-process kernel code; its price is that the process model in 0011 § 5 becomes a second
realisation of a seam the design has not yet named, and one kernel process
carries every Linux process of a user. The client-environment question has a
clear answer: the axis that forces two substrates is N against H (a client
with no hypervisor cannot run H at all), not A against B. Within H, B
dominates A on every axis but paperwork, and the paperwork is cheap today
because the core is one syscall wide.

## What WHP fixes, and what it leaves open

The constraints below are the ones the shapes are built around. The first
five are documented behaviour of the Windows Hypervisor Platform; the rest
are measured or unmeasured as marked.

A partition belongs to the NT process that created it. Its vCPUs run only on
threads of that process, through `WHvRunVirtualProcessor`, and the handle is
not something another process can use. Guest physical memory is host virtual
memory of that process, mapped range by range with `WHvMapGpaRange`; the same
host range may be mapped into more than one partition, which is how memory
can be shared between guests that live in one process. A process may own
several partitions. The API lives in `WinHvPlatform.dll`, a Win32 DLL;
`spike/whp/whp-probe.c` includes `<windows.h>` and links against it.

Measured, on `ins-15` only: a ring-3 `syscall` round trip through a ring-0
`hlt` shim costs a median 5.0 µs (spike 40); a cancel from another thread
lands in a median 4.1 µs and is latched exactly once when the vCPU is not
running (spike 41); an unmapped GPA produces a memory-access exit at the
right address (spike 40, q7).

Unmeasured, and load-bearing for both shapes: what `WHvCreatePartition` plus
`WHvSetupPartition` cost; what `WHvMapGpaRange` costs per call and whether it
pins host pages; how many partitions one process may hold and how many vCPUs
one partition may carry; whether a host page behind a mapped GPA may be
decommitted; whether `RtlCloneUserProcess` succeeds on a process that owns a
partition and has `WinHvPlatform.dll` loaded, and whether the child can create
a partition of its own.

## Shape A: one host process per Linux process

Each `lk-host` owns one partition. A Linux thread is a vCPU in that partition
driven by an NT thread. `fork` is `RtlCloneUserProcess` of the host, as under
N, and then in the child: close the inherited partition handle, create a new
partition, set it up, `WHvMapGpaRange` every backed range of the cloned host
memory into it, create one vCPU, load the parent's register state, run.
Copy-on-write is NT's, from the process clone. Everything in 0011 § 5, § 7 and
§ 12 that crosses processes (pids over ALPC, signal ports as named sections,
IPC as named objects, the supervisor's handle to every process) stays as
written.

What A buys.

The documents stand. The nine-call interface keeps its shape; `as_*` calls
need no address-space argument because the address space is the process.
Substrate-Interface.md, Architecture.md § Process shape, and 0011 § 5 need
substrate qualifiers, not new mechanisms.

Isolation. A fault in one process's kernel instance kills that process. A
Linux process is an NT process, so Task Manager, job objects, `NtSuspendProcess`
for `SIGSTOP`, the supervisor's exit-cleanup wait on a process handle, and a
Windows-side `taskkill` all mean what they mean under N.

Sharing the N code path. The supervisor, the signal port, the ring pipes, the
descriptor-passing `NtDuplicateObject` into a peer process are identical under
N and A, so the cross-process layer is written once.

What A costs.

Fork. The only clone that runs a thread measured 5.1 ms on a tiny process
(spike 35, q8), against criterion 4's 2 ms; A adds partition creation and
setup, a `WHvMapGpaRange` per backed range, and a vCPU creation, none of it
measured and all of it plausibly milliseconds. A `configure` script that forks
ten thousand times pays that ten thousand times. Under A, criterion 4 is not
going to pass on this host, and no measurement suggests otherwise.

The host is Win32. `WinHvPlatform.dll` needs `kernel32`, so `lk-host` loads it,
registers with `csrss`, and the argument § 2 gave for a native process (a clone
with no `csrss` connection has nothing to go stale) is gone. `RtlCloneUserProcess`
of a Win32 process is what Windows Error Reporting's snapshots do, so the
primitive works; what is unknown is whether a clone that then lives for
minutes and calls `WHv*` and heap functions works, and whether
`WinHvPlatform.dll`'s own per-process state (worker threads that do not exist
in the child, locks that a vanished thread held) survives. The child can
re-register with `csrss` through undocumented `CsrClientConnectToServer`, which
is how the public fork-on-Windows demonstrations handle it; that is one more
undocumented interface on the list in 0011's cross-cutting section.

Two translations for every cross-process memory access. A user address under
H is a guest virtual address. `ptrace(PEEKDATA)` from a tracer process has to
walk the tracee's guest page tables, which live in the tracee's host memory,
through `NtReadVirtualMemory`, then read the page they name, also remotely.
`/proc/<pid>/mem`, `process_vm_readv` and core dumps of another process take
the same path. It is implementable and it is the kind of code that is wrong
for a year.

Hypervisor objects per process. A `make -j12` over a kernel-sized tree runs a
few hundred processes at once, each a partition with its own second-level page
tables. Hyper-V is engineered for tens of virtual machines per host, not
hundreds of partitions per user; the limit and the per-partition memory cost
are unmeasured.

Every cross-process futex, robust list, and lock stays a problem. Risk 10's
lost-wake protocol through the supervisor is A's problem exactly as it is N's.

## Shape B: one kernel process per user, every Linux process inside it

The supervisor and the kernel become one Win32 process, `lk-kernel`, holding
one partition (B2) or one partition per Linux process (B1). A Linux process is
a page-table root in guest physical memory plus the kernel's own structures
for it: VMA tree, descriptor table, signal state, credentials, all in the
kernel process's heap. A Linux thread is a task the kernel schedules onto a
vCPU: under B2 the vCPUs are a pool, sized to a small multiple of the host's
processor count, and a task that enters a syscall gives its vCPU up while the
kernel works on its behalf; a task blocked in an NT wait holds an NT thread
and no vCPU. That is gVisor's KVM platform, thread for thread.

`fork` is `dup_mm`: copy the parent's page tables into a new root, mark every
writable leaf read-only in both, share the GPA pages, and copy a page when a
write exit arrives. `vfork` and `CLONE_VM` are a new task on the parent's root,
which is the only correct realisation of what glibc 2.28's `posix_spawn` asks
for (review risk 6). `execve` is a fresh root and a teardown of the old one,
in the same partition. `wait4`, `kill`, `ptrace`, process-shared futexes,
`SCM_RIGHTS`, SysV IPC, the pty line discipline and advisory locks are
function calls on structures in one heap, with no ALPC, no named sections and
no protocol between processes.

What B buys.

Fork at kernel speed. Copying page tables for a 100 MB process is on the
order of a hundred microseconds; nothing about it involves NT process
creation or the hypervisor's partition machinery. It is the only shape on the
table with a credible path to criterion 4, and the number is measurable in
the whp spike's harness before anything else is built.

The cross-process layer disappears. 0011 § 5 (pids, wait, the process-info
section, exit cleanup by handle), § 7's signal ports, § 12's named IPC
objects, the supervisor's lock table and the keyed-wait fallback for shared
futexes are all replaced by what a kernel normally has: tables in its own
memory. Risk 10 (lost wakes), risk 4's commit-charge doubling at fork, and
DR-0029's whole subject (what crosses a process clone and how the child
audits it) cease to exist under H.

No process clone, no thread hijack, no executable anonymous memory in the
host. The kernel process never calls `RtlCloneUserProcess`, never rewrites an
NT thread's context, and holds guest code as data pages that the hypervisor
executes through second-level translation. Of every shape this project has
considered, this is the one that looks least like malware to an endpoint
product, and it is immune to a CET-strict policy that would break N's hijack
and DR-0030's `iretq`.

`user_copy_*` and `ptrace` are one translation. GPA to host address is a
table the kernel owns; guest virtual to GPA is a page-table walk over memory
in the same process.

Interop is unchanged from 0011's own model. `lk.exe` still asks the kernel
process to run a program and waits; a PE `execve` is still a `CreateProcess`
from the kernel process with handles pumped; the console bridge is unchanged.

What B costs.

The process model becomes a second realisation. Under N a Linux process is an
NT process and must be, since N has no page tables of its own; under B it is
not. Everything 0011 puts in the supervisor is therefore reached over ALPC
under N and by a call under B. The honest architecture is three layers, not
two: the core; a process-transport seam (pid table, wait, signal send, tty,
IPC namespaces, locks) with an in-process and a cross-process realisation;
and the substrate. 0011 already draws the supervisor's boundary in § 2 and
§ 5, so the seam exists in prose; what changes is that it becomes an interface
with two implementations rather than one process with one protocol.

The interface acquires an address-space object. `as_map(as, ...)` and its
siblings, plus `as_create` and `as_destroy`. N realises `as` as its own
process state and ignores the argument's generality. This is cheap now,
because the core is Phase 1 and the conformance suite is small, and it is
expensive after Phase 2 has written a VFS that assumes one address space per
kernel instance.

One process, one blast radius. A host-side kernel bug kills every Linux
process of that user, where under N or A it kills one. 0011 already accepts
this for a supervisor crash ("a kernel panic for that user's session"); B
widens it to every fault. Real kernels have the property and Rust for the
kernel (0011 open question 4) is the mitigation this project already leans
toward.

The kernel copies its own structures at fork. Under N and A, NT's clone
carries the VMA tree and descriptor table for free and the core needs only
per-kind `fork_fixup`; under B the core needs `copy_mm`, `copy_files`,
`copy_sighand` written out, which is ordinary kernel code and is also the
code that removes the stop-the-world barrier N's fork needs (a JVM calling
`Runtime.exec` under N stalls every thread for the length of the clone; under
B it copies one process's tables).

A single process's resources are everyone's. Thread count is the number of
Linux threads blocked in NT waits plus the vCPU pool, which NT handles into
the tens of thousands; host virtual address space (128 TB) holds the physical
memory of every guest, which is not a practical limit; handle count is not a
limit. What is a limit is the vCPU count per partition (unmeasured; Hyper-V's
per-VM ceilings are in the low hundreds, which a pool never approaches) and
whether mapped guest memory is pinned, which under B pins one process's
working set to the sum of every Linux process's touched memory.

Windows sees one process. `tasklist` shows `lk-kernel.exe`; a Windows user
cannot `taskkill` one Linux process; per-process Windows tooling (Process
Explorer's per-process view, ETW by pid) sees the kernel. A `/proc`-shaped
view and `kill` from a Linux shell are the tools instead, as inside any VM.

B1 against B2. B1 (a partition per Linux process, all in one host) keeps
partition creation on the fork path and forgoes vCPU pooling, for the benefit
of never switching a vCPU between address spaces. B2 (one partition, one
page-table root per process) switches by setting guest CR3 from the host
before `WHvRunVirtualProcessor`, a register write that is already on the
syscall return path, and pays no per-fork partition cost. B2 is gVisor's
choice and the one this note means by B.

## Side by side

| Axis | A: host per process | B2: one kernel process | Evidence |
|---|---|---|---|
| fork cost | NT clone (5.1 ms measured, small process) + partition create/setup/map/vCPU (unmeasured) | page-table copy, ~100 µs class (unmeasured) | spike 35 q8 |
| fork COW mechanism | NT's, via `RtlCloneUserProcess` | kernel's, via read-only PTEs and write exits | 0011 § 4 text describes B, not A |
| vfork / `CLONE_VM` | no mechanism; `posix_spawn` breaks | a task on the parent's root | glibc 2.28 `spawni.c` |
| host process kind | Win32 (`WinHvPlatform.dll`) + clone of a `csrss`-registered process | Win32, never cloned | `spike/whp/whp-probe.c` |
| signals to a spinning thread | cancel via the target's signal thread, cross-process port | cancel, in-process | spike 41 |
| ptrace, `/proc/pid/mem`, cores | two remote translations | one local walk | — |
| process-shared futex | supervisor keyed wait, lost-wake protocol | ordinary futex | review risk 10 |
| commit charge at fork | doubles (NT clone) | per copied page | — |
| isolation on kernel fault | one process dies | the user's session dies | 0011 cross-cutting, already accepted for the supervisor |
| hypervisor objects | one partition per process; hundreds under a parallel build | one partition, a vCPU pool | limits unmeasured |
| Windows-visible processes | one per Linux process | one | — |
| EDR profile | clone + context rewrite of own threads (hollowing-shaped) | none of it; guest code is data to the host | AGENTS.md endpoint note |
| CET-strict policy | breaks hijack under N; A does not hijack, so survives | survives | spike 38 q5 ran with CET off |
| interface change | none | `as` handle, `as_create`, `as_destroy` | cheap at Phase 1 |
| core change | none | process-transport seam with two realisations | 0011 § 2, § 5 already draw it |
| documents | substrate qualifiers | § 2, § 5, § 7, § 12 gain an H variant | — |

## Does a deployment need both?

Two client properties decide what runs, and they are independent of each
other.

Whether the hypervisor is available. WHP needs virtualization enabled in
firmware and the "Windows Hypervisor Platform" optional feature; inside a
virtual desktop it needs nested virtualization exposed by the outer
hypervisor, which Citrix, VMware Horizon and most cloud desktop offerings do
not do by default and some forbid by policy. A client of that kind cannot run
H in either shape. That is the case Architecture.md already names as forcing
N, and it is the reason N exists at all once H is on the table.

Whether the userland may be rebuilt. H runs Rocky 8's own RPMs; N runs a
rebuilt distribution and, until the census in review risk 7 says otherwise,
one without Go-built binaries or the sanitizers. A client that needs stock
el8 packages, a vendor's binary, or `dnf` against the public mirrors needs H.
A client that will take a rebuilt distribution from this project can use N.

Those two axes produce four cells. Hypervisor present and rebuild acceptable:
either substrate; H for fidelity and EDR profile, N for syscall latency
(1 µs against 5 µs) and a smaller footprint. Hypervisor present and stock
binaries required: H only. Hypervisor absent and rebuild acceptable: N only.
Hypervisor absent and stock binaries required: nothing this project offers,
short of the dynamic-translation option 0011 names as a third substrate. So
yes, a project that serves more than one kind of client needs both N and H,
and the reason is the client's hypervisor, not anything about A or B.

Within H, no client property selects A over B. A has no capability B lacks.
Its one operational distinction, that Windows sees a process per Linux
process, is a convenience for Windows-side tooling, not a requirement any
client has stated, and it comes bundled with the fork cost and the clone
hazards above. A deployment would pick A only if B's single-process blast
radius were unacceptable, and a client with that requirement is one that
would also reject the supervisor 0011 already has.

The consequence for the architecture is the three-layer shape named under
B's costs. If both N and H are to exist, the core sits on a process-transport
seam with two realisations (cross-process for N, in-process for H) and on a
substrate interface with an address-space handle. Neither is a rewrite of
anything that exists today: the core is Phase 1, the substrate suite is nine
groups, and the supervisor is unbuilt. Both become rewrites the moment Phase 2
and Phase 3 are built against ALPC alone.

## What decides it, in order

Each is a spike in the whp harness, most of them an afternoon.

1. `WHvMapGpaRange`: cost per call at 4 KB and 2 MB; whether the host
   working set grows by the mapped size (pinning); whether a decommit behind
   a mapped GPA is refused, ignored, or honoured. Decides whether H's lazy
   memory story holds in either shape.
2. Partition ceilings: vCPUs per partition, partitions per process, memory
   per idle partition. Decides B2 against B1, and whether A is viable at all
   under a parallel build.
3. Page-table copy and CR3 switch in one partition: fork a 100 MB guest
   process by copying its tables and switching a vCPU to the new root; time
   it; confirm a write exit on a shared page and a copy that leaves the other
   side intact. That is B's `as_clone`, measured before it is designed in
   detail.
4. `RtlCloneUserProcess` of a process holding a partition, with
   `WinHvPlatform.dll` loaded: does the child run, can it create a partition,
   does anything in the DLL deadlock. Decides whether A is even available.
5. Partition create, setup, map and first run, timed end to end. A's fork
   floor.

If 1 comes back with pinning and no lazy path, H is in trouble in both
shapes and the question moves to whether the dynamic-translation substrate
is the second one. If 3 comes in well under a millisecond, B has done what A
cannot and the choice is made on evidence.

## Measured, 2026-09-05

The five measurements above were taken the same day, as four spikes:
`spike/whp-gpa-map/`, `spike/whp-partition-cost/`, `spike/whp-pagetable-fork/`
and `spike/whp-clone-host/`, rows 42 to 45 of `doc/milestones.md`, all four
registered with the runner and reproducing. What they say, against the
questions as posed:

1. `WHvMapGpaRange` is lazy and pins nothing. A committed, untouched
   gigabyte maps with the working set unmoved and no page resident; the
   guest's first touch backs a page at about 13 µs (4 µs once resident) and
   leaves it unlocked. Reserve-only host memory maps, and a touch behind it
   arrives as a memory-access exit that says "mapped, host absent",
   distinguishable from an unmapped GPA; commit on the exit and resume works
   at about 32 µs a page. Decommit and release behind a live mapping succeed
   and exit the same way. Populate advice over 64 MB costs 6 ms and makes the
   first touch cost what a resident page does. H's memory story holds in
   both shapes, and the lever is populate-in-ranges, not map-on-fault.

2. One mapped partition per process. Several partitions set up, but the
   second `WHvMapGpaRange` into a second partition is refused (`0xC0370008`)
   until the first unmaps; a second process is unaffected, and a section
   viewed in two processes maps into both partitions coherently. B1 is out.
   B2's preconditions hold: `ProcessorCount` to 2048, 240 vCPUs created and
   run (Hyper-V's ceiling), a vCPU handed between two threads a thousand
   rounds each, eight vCPUs exiting at once at 4 to 6 µs each against 3.6 µs
   alone. A partition costs 0.4 ms to set up, 0.7 ms to a first exit.

3. Page-table fork works and costs what the tables cost: about 0.1 ms for
   64 MB and 0.5 to 0.75 ms for 576 MB, against spike 35's 5.1 ms clone. A
   guest `#PF` reaches the host as an exception exit with the fault address,
   error code and `%rip` intact; the stale translation needs no flush. Parent,
   child and grandchild isolate correctly on every path tried. The cost that
   is not free is the copy-on-write fault itself, about 25 µs over a faultless
   store, ten to twenty times Linux's; switching a vCPU between roots is about
   6 µs.

4. A clone of a partition-holding Win32 process runs, does ordinary Win32
   work (not `LoadLibrary`), and builds its own partition to a first exit in
   1.3 to 2.3 ms. The inherited partition handle hangs the child if it is the
   child's first WHP call and drives the parent's guest if it is not; the
   design closes it first. Shape A is available.

5. The clone costs 3 to 5 ms with 4 MB mapped and 6 to 8 ms with 256 MB
   touched, and the same with that memory unmapped: mapping adds nothing to
   the clone. Shape A's fork floor is therefore the NT clone (scaling with
   touched memory) plus about 2 ms of partition, before the child remaps its
   guest memory, which was not measured and is the part that scales with the
   address space.

What that does to the choice. The measurement that could have sunk H in
both shapes (pinning) came back clean. The one that could have sunk shape A
(the clone) came back working. The ceiling nobody expected (one mapped
partition per process) removes B1 and leaves B2 exactly as described. And
the number the two shapes were always going to be judged on, fork, came back
where the arithmetic said it would: shape B copies tables in under a
millisecond at 576 MB; shape A pays the NT clone, 3 to 8 ms and rising with
touched memory, plus 2 ms of partition, plus a remap. B2 for H, as
recommended, now on measurement rather than on argument; the one new cost to
carry into the design is the 25 µs copy-on-write fault, which argues for
`vfork` where glibc offers it and for eager copying of what a child will
certainly touch.

## Not verified

That `WHvRunVirtualProcessor` may be called for any vCPU from any thread of
the owning process, one thread per vCPU at a time. Measured since for two
threads over one vCPU (spike 43, q5); a pool over many threads is not.

That the one-mapped-partition-per-process ceiling is WHP policy rather than
this build's. It was measured, not looked up.

Why `LoadLibrary` fails in a clone, and why the inherited handle hangs as a
first call. Both are reported as behaviour, not diagnosed (spike 45).

That shape A's child remap scales acceptably. The clone and the partition
were timed; the remap of the child's guest memory was not.
