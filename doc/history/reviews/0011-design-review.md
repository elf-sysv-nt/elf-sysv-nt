# Design review: a Linux personality for NT

History, not design. A review of proposal 0011 written 2026-09-05 at main
`8a6a53b`, before the six spikes of that day ran, answering a request that
asked for a ranked risk list and a verdict on the two-substrate claim. Two of
its claims were refuted by measurement and are corrected in the section
"Measured since" at the end rather than edited above; proposal 0012 and
DR-0098 to DR-0100 are what the design became. Read it for the ranking and
the arguments, not for the state of the tree.

Read: proposal 0011 in full,
`Substrate-Interface.md`, `Architecture.md`, `Address-Space.md`,
`Substrate-N.md`, `Core-Phase1.md`, DR-0003, DR-0029, DR-0030, DR-0063,
DR-0097, and the transcripts under `spike/` for arena, hijack, lxfs,
native-host, nt-clone-fork, whp, whp-vcpu-interrupt, fs-base-persistence,
fs-base-fault, gs-thread-pointer and vendor-image-shape.

The short answer first. The core is sound, and the choice of boundary is the
right one; nothing below says otherwise. The two-substrate claim does not hold
for the design as written, because H's process model was never designed: the
sentences that describe fork, address space and I/O under H describe a kernel
that lives inside the guest, and this kernel lives outside it. N carries two
risks that a spike can settle in days, one of which (the red zone on a
synchronous fault) may reverse a decision record. The rest is measurement and
bookkeeping.

Numbers below are from one host (`ins-15`, Windows 10.0.26200.9168, an AMD
Ryzen 5 7530U) and are existence proofs, not portability claims.

## Ranked risks

Ordered by what a wrong answer costs. Each opens with what the risk is and a
verdict; the paragraph under it says what was asserted, what was measured, and
what is missing.

### 1. Under H, `fork` and the process model are undesigned

Verdict: fatal to the two-substrate claim as written; not fatal to the core.

0011 § 4 says of H: "fork marks every writable entry read-only in parent and
child and copies on the first write fault", and `Substrate-Interface.md` gives
`as_clone` under H as "duplicating guest page tables copy-on-write".
`Architecture.md` calls it "the ordinary one". Those are true of a kernel that
runs in the guest and owns the guest's memory. Here the kernel is a host-side
sentry: the VMA tree, the descriptor table and the signal state are NT heap in
`lk-host.exe`, and the guest is a WHP partition that process owns. A child
therefore needs a copy of the host process's kernel state and a second
partition. There are two shapes that give it one, and the documents have
chosen neither.

Shape A keeps N's topology, one host process per Linux process. `fork` is then
`RtlCloneUserProcess` of the host (as under N; spike 35 timed it at a median
5.1 ms) plus, in the child, a new partition, a `WHvMapGpaRange` for every
backed range, and a vCPU rebuilt from the parent's registers. Copy-on-write
comes from NT's clone rather than from any page table the kernel writes, so the
page-granular fork the documents promise is NT's, not H's. Nothing has measured
partition creation and setup cost, whether a cloned process can create a
partition at all, or what `WHvMapGpaRange` costs per call; spike 40 mapped one
4 MB region once. Shape A also collides with § 2: `WinHvPlatform.dll` imports
`kernel32`, and `spike/whp/whp-probe.c` includes `<windows.h>` and runs as a
Win32 process. An `lk-host` that imports only `ntdll` cannot call `WHv*`. So
under H either the host is a Win32 process (and the reason § 2 gave for making
it native, that a clone with no `csrss` connection has nothing to go stale, is
given up) or the partition lives in another process and the vCPU threads with
it.

Shape B is one host process holding many partitions, each Linux process a set
of guest page tables, each thread a vCPU. That is gVisor's shape on KVM, and it
is the shape every hypervisor-backed sentry has ended up with. Under it `fork`
really is page-table duplication and copy-on-write is the kernel's own, done by
mapping GPAs read-only and remapping on the write exit. But under shape B the
per-process supervisor split in § 2, the "kernel state is memory in this
process, so fork is a clone" argument in § 5, the per-process signal ports in
§ 7 and the `RtlCloneUserProcess` mechanism are all N's, and every `as_*` call
in the interface acquires an address-space argument it does not have today.
That is the interface being N-shaped: it has no address-space object because
under N the address space is the process.

What the design should be instead: decide the topology before another word is
written about H, and write § 5 for it. If shape A, measure partition setup and
the clone-with-partition case, and resolve the `kernel32` dependency. If shape
B, accept that H is a second design sharing the core and the syscall table,
not a second implementation of nine calls, and say so. Either way the sentence
"under H every one of those paragraphs is simpler" is withdrawn.

A smaller leak of the same kind sits in § 5: `SIGSTOP` is `NtSuspendProcess`
on the supervisor's handle. Under H that suspends the NT threads driving the
vCPUs, and a thread suspended inside `WHvRunVirtualProcessor` does not stop
the guest until the next exit; a spinning guest keeps spinning. The fix is a
`WHvCancelRunVirtualProcessor` first, and it means the supervisor knows which
substrate it is stopping, which invariant 3 says it must not.

### 2. Guest memory under H: pinning and lazy backing are unmeasured

Verdict: needs a measurement before H is committed to.

§ 4 says the lazy-commit handler "still exists, one level down" under H. That
holds only if the hypervisor tolerates a mapped GPA whose host page is not
committed, or if the kernel maps GPAs on demand from memory-access exits.
Spike 40 (`spike/whp/`) mapped 4 MB of committed memory and confirmed that an
unmapped GPA produces a memory-access exit (q7). It did not measure whether
`WHvMapGpaRange` pins the host pages, what it costs per 4 KB or per 2 MB
range, or whether a host decommit behind a mapped GPA is permitted. If mapped
memory is pinned, a 64 GB `MAP_NORESERVE` reservation under H is either 64 GB
of resident RAM or a first-touch exit per page, at about 5 µs plus the map
call; a program touching 1 GB of fresh memory would pay several seconds where
Linux pays a tenth of one. The experiment is a page of C: map 4 GB, read the
working set, time the map call at both granularities, decommit behind it and
touch from the guest.

### 3. The red zone on a synchronous fault under N

Verdict: needs a measurement; a failure reverses DR-0050 for substrate N.

DR-0050 retired `-mno-red-zone` because the repair DR-0006 asked for had
landed: WP-43's delivery, certified by `runtime/signal/t/run.sh` and recorded
in DR-0030, leaves the 128 bytes below `%rsp` intact. Every measurement behind
that chain, from `spike/redzone-delivery/` onward, is of the asynchronous
path: a hijack from another thread, with the frame built below the red zone.
Spike 38 (`spike/hijack/`, q2) confirms that path keeps it. The synchronous
path was not measured, and it is the one N's address space depends on. A first touch of a
lazily committed page (§ 4, spike 37 q6), a `SIGSEGV`, a `SIGBUS` past a file's
end, a `SIGFPE`, an `int3` under `ptrace`: each is an NT exception dispatched
to the faulting thread, and NT dispatches by writing an `EXCEPTION_RECORD` and
a `CONTEXT` onto that thread's user stack, directly below `%rsp`, before
`KiUserExceptionDispatcher` calls any vectored handler. The Windows x64 ABI has
no red zone, so nothing skips it. A leaf function holding temporaries below
`%rsp` that stores into a fresh anonymous page has those temporaries overwritten
before the handler that commits the page runs, and the instruction is then
re-executed over corrupted locals. Under H the fault is a VM exit and the guest
stack is untouched.

The experiment: a leaf that fills its red zone with a pattern, touches an
uncommitted page, and checks the pattern after resume. If it fails, the
choices are `-mno-red-zone` as a default of N's toolchain (a rebuilt userland
can carry it; that is what a rebuilt userland is for), or eager commit for all
anonymous memory under N, which gives up `MAP_NORESERVE` and charges the commit
limit for reservations a JVM or a sanitizer makes. Hand-written asm that uses
the red zone is rare enough to record as a divergence. Either way DR-0050's
reasoning was about a path that N does not use for faults, and the record
should say so.

### 4. `fork` is 5 ms on the only measurement, against a 2 ms criterion

Verdict: needs a measurement before criterion 4 or § 5's cost claims stand.

§ 5 promises `fork` "in under two milliseconds" and criterion 4 makes it a
test. Spike 35 (`spike/nt-clone-fork/`) measured `NtCreateProcessEx` with a
null section at 521 µs, and found no thread can run in that clone (q5, q10,
q11, `0xc000010a` `STATUS_PROCESS_IS_TERMINATING` from `NtCreateThreadEx`
against a clone whose exit status says it is running, from a native parent as
well as a Win32 one). The clone that does run a thread, `RtlCloneUserProcess`,
measured 5145.8 µs median over 200 iterations, against Cygwin's 9942.5 µs. D2
in 0011's decision log records "the clone works through the wrapper" and does
not record that the wrapper misses the proposal's own target by a factor of
two and a half. The spike's process was tiny; a 200-VMA, 64-descriptor process
is what the criterion names, and NT's clone charges commit for every private
page it marks copy-on-write, so the number should be expected to grow with
RSS. A 10 GB process that forks to snapshot itself (Redis, some JVM tooling)
will fail with `STATUS_COMMITMENT_LIMIT` where Linux's overcommit lets it
succeed; that one is a divergence to record, not to fix.

Three measurements are missing: `RtlCloneUserProcess` from an ntdll-only
parent (q11 ran the raw primitive from one, not the wrapper); the same call at
realistic RSS and VMA count; and a profile of where the ten-fold gap between
the raw clone and the wrapper goes, since a 521 µs primitive that cannot start
a thread and a 5 ms one that can suggest the thread handshake, not the
address-space copy, is the cost. A `configure` script forks on the order of
ten thousand times; at 5 ms that is fifty seconds of fork on a machine where
Linux spends one.

### 5. How I/O reaches user memory is unstated, and the answer differs by substrate

Verdict: needs a design decision before phase 2; the VFS is N-shaped without it.

Invariant 2 says the core touches user memory only through `user_copy_*`. The
VFS bodies in § 8 call `NtReadFile` and `NtWriteFile`, and a body that hands
NT the user's buffer is not dereferencing it in any sense
`bin/check-substrate-line` can see. Under N that zero-copy path works for
committed memory and fails for a lazily committed page: the I/O manager
probes the buffer in kernel mode, the vectored handler never runs, and the
call returns `STATUS_ACCESS_VIOLATION`. Under H the buffer is a guest address
and NT cannot take it at all; the kernel must translate through its own page
tables and copy, one page at a time, because the host pages behind a guest
range need not be contiguous. gVisor avoids both problems by keeping a host
mirror of every application mapping at the same virtual address, which is why
its sentry can pass buffers to the host kernel; 0011 has neither the mirror
nor a stated bounce rule.

The decision is between a mirror (host VA equals guest VA, which under N
means pre-committing before every I/O and under H means a second mapping of
everything) and bounce buffers everywhere, one copy per `read` and `write`.
Bounce is substrate-neutral and costs a memcpy; it is the honest reading of
invariant 2, and it should be written down as the rule before the VFS exists,
because a VFS written zero-copy under N is a VFS that has to be rewritten for
H. Phase 1's `write` of six bytes did not exercise the question.

### 6. `CLONE_VM | CLONE_VFORK`, and glibc 2.28's `posix_spawn`

Verdict: needs a design decision; fatal to "unmodified el8 glibc" under H until made.

§ 6 says `clone` "with `CLONE_VFORK` but without `CLONE_VM` is the `vfork`
glibc's `posix_spawn` uses", implements it as fork plus wait, and returns
`EINVAL` for every other combination. glibc 2.28's
`sysdeps/unix/sysv/linux/spawni.c` calls `clone` with
`CLONE_VM | CLONE_VFORK | SIGCHLD`, and the child reports an `exec` failure by
writing `args->err` in the parent's memory; that write is how
`posix_spawn("/nonexistent")` returns `ENOENT` rather than a child that exits
127. glibc's `vfork()` is the `vfork` syscall itself, number 58, whose
contract is the same shared address space. So the design as written returns
`EINVAL` to the one `clone` shape `posix_spawn` makes, and gives `vfork`
semantics no shipped binary was built against.

Under N the fix is in the `sysdeps` port: `spawni.c` reverts to the
pipe-based error report glibc used before 2.24, and `vfork.S` becomes a fork.
That is two files § 16 does not list, and a divergence to record for any
program that relies on `vfork`'s shared memory. Under H nothing is rebuilt, so
the kernel must implement a child that runs on the parent's address space
until `execve`. Shape B in risk 1 gives that for free, because the child is a
new page-table root over the parent's pages; shape A has no mechanism for it
short of running the vfork child as a thread of the parent's host process with
its own pid and transplanting at `exec`, which is a design paragraph nobody has
written.

### 7. The gate: restart semantics, and the `syscall` census

Verdict: needs a measurement (the census) and a design paragraph (restart).

Replacing the instruction with `call *_dl_sysinfo(%rip)` is sound in the
places the request lists, with two exceptions. `rt_sigframe` is unchanged;
`rt_sigreturn` reached by `call` overwrites the consumed `pretcode` slot and
nothing else; `orig_rax` is a saved word the gate already has; `ptrace`
syscall-stop and `seccomp` hang off the gate's entry, and `SECCOMP_RET_TRAP`'s
`si_call_addr` can only be the return address, since the kernel does not know
the call instruction's length. `clone` works because `thread_start` gives the
child the caller's return address on the new stack. Delivery into the gate's
first instruction is harmless: the frame goes below the pending return
address and the syscall runs after the handler.

The first exception is restart. Linux restarts an interrupted syscall by
backing `%rip` up two bytes and restoring `%rax`; the gate cannot back up an
unknown-length `call`, and re-executing a `call` would push a second return
address. The working shape is a vDSO stub, `jmp *gate`, which the signal frame
names as `%rip` with `%rsp` still pointing at the original return address and
`%rax` reset to the number. § 7 lists `SA_RESTART` among things that are
"kernel-side, all Linux's semantics", which reads as if it were free; it is a
mechanism this design has to invent, and a handler that reads
`uc_mcontext.gregs[REG_RIP]` after a restarted syscall will see the stub. Write
it down before the signal work in phase 3.

The second is the census. § 16's post-link check refuses any image with an
`0f 05` in executable text or a `%fs` prefix. What that refuses has not been
counted: `spike/vendor-image-shape/` read three packages and 41 ELF files, all
glibc's. Go's runtime issues raw `syscall` and reaches `g` through `%fs`, so
every Go-built binary in el8 (`podman`, `buildah`, `skopeo`, `runc`,
`containerd`) fails the check unless the Go toolchain is ported too, which § 16
does not scope. `libasan` and its siblings use raw syscalls; `mono` and
`dotnet` use `%fs`. The experiment is one afternoon: scan BaseOS and AppStream
for the two byte patterns outside glibc and list the packages. Its result is
the size of N's userland, and it should be known before the toolchain work
starts.

### 8. Address space: 64 KB against 4 KB, and what `ld.so` actually does

Verdict: needs a measurement on the version floor; a trap only if the coarse path stops being tested.

Spike 37 (`spike/arena/`) measured a 64 TB placeholder (127 TB refused), a
4 KB split, a 4 KB pagefile view, a 4 KB file view at a 4 KB file offset, and
lazy commit through a vectored handler at 4108 ns per fault against 10 ns for
a plain touch. D4 parked the choice between the documented 64 KB and the
measured 4 KB. The question that decides it is not taste but what
`_dl_map_segments` does on every `dlopen`: it maps the object's whole span
from the first segment's file offset (a span that runs past the end of the
file for any object with a 2 MB alignment gap, which NT refuses as a single
view and Linux allows with `SIGBUS` on touch), then `mmap(MAP_FIXED)` each
later `PT_LOAD` over part of that view at a different file offset, then
`mprotect(PROT_NONE)` the holes. el8's objects put every segment on a 2 MB
boundary (spike `vendor-image-shape`: 82 of 82 `PT_LOAD` at `p_align`
`0x200000`), so the 64 KB rule is never tested by the distribution's own
binaries and only bites objects linked below the granule: Go's, and anything
built with a later binutils' `-z separate-code`.

Two consequences follow. A `MAP_PRIVATE` view that has been written to holds
private copy-on-write pages, and a partial `munmap` or a `MAP_FIXED` over part
of it under N means unmapping the whole view and remapping the remainders,
which discards those pages unless the kernel copies them out first. That
needs designing whichever granularity holds. And the 4 KB result is from one
kernel build; the design floor is Windows 10 1809. Rerun spike 37 on the floor
and on Server 2022 before choosing. If 4 KB holds there, build against it and
drop the pretence; if not, keep the 64 KB path and put a switch in the
conformance suite that forces it, so the fallback stays green rather than
theoretical. Building against a rule the shipped binaries never exercise is
how a fallback rots.

### 9. NTFS semantics the VFS has not yet met

Verdict: two measurements; the rest acceptable as recorded divergences.

Spike 39 (`spike/lxfs/`) confirmed the LX metadata round trip, the symlink
reparse point without privilege, per-directory case sensitivity, POSIX delete
of an open file, `NtQueryInformationByName` at 9659 ns per stat against
Cygwin's 85976, and that WSL reads the tree back while Cygwin 3.6.10 sees 755
and a regular empty file where a device should be. Criterion 3 should lose its
Cygwin half; it is measuring Cygwin, not this design.

Not measured, and each has bitten WSL1's DrvFs: `FILE_RENAME_POSIX_SEMANTICS`
over an open target (q6 measured delete only); `unlink` and `ftruncate` of a
file with a live section view, which NTFS refuses with `STATUS_CANNOT_DELETE`
and `STATUS_USER_MAPPED_FILE` where Linux permits both (`git gc` over mapped
packs, BerkeleyDB's `__db.*` under `rpm`, and therefore `dnf` under H, are the
callers to worry about); and renaming a directory while any handle is open
beneath it, which NT refuses and every shell whose cwd is inside triggers.
Add the first two to spike 39's rerun. The third is a recorded divergence
Cygwin shares. The per-open cost of `NtCreateFile` through the filter stack,
tens of microseconds against Linux's one, is the WSL1 complaint that applies
here in full; it is acceptable against the stated goal of rivalling Cygwin,
and it should be written down as a number rather than left to be discovered by
the first `./configure`.

### 10. Process-shared futexes and futex timeouts

Verdict: needs a measurement and a stress test before phase 7; not fatal.

`RtlWaitOnAddress` is per process (spike 36 q4 measured it within one). § 6
sends process-shared waits through the supervisor as "a keyed wait, slower and
correct". Correct is the part to prove: a waiter that compares the word
locally and then registers with the supervisor races a waker whose message
arrives first, and the lost wake is a hang under exactly the contention that
PostgreSQL 10's `sem_init(pshared)` semaphores in `/dev/shm` produce. Linux
avoids it by doing the compare under the hash-bucket lock. NT's keyed events
(`NtWaitForKeyedEvent` on a shared named object, key derived from device,
inode and offset) give a cross-process rendezvous without a supervisor
round-trip, but the same race exists unless a waiter count lives somewhere
both sides can see. It is solvable and it is subtle; criterion 12 is the test
and it should be run under contention, not once.

The other divergences here are survivable. `FUTEX_REQUEUE` waking instead of
requeueing does not reach glibc 2.28's condition variables, which stopped
using requeue in 2.25. Wake-all for `1 < n < INT_MAX` is a fairness delta.
No priority inheritance is a delta only for real-time code. What is not
mentioned anywhere is timeout resolution: `RtlWaitOnAddress` wakes on the
system clock interrupt, 15.6 ms unless some process has raised it, and a
`pthread_cond_timedwait` of 1 ms that sleeps 15 is visible to anything with a
latency budget. Measure the wake distribution at 100 µs and 1 ms, and expect
to call `NtSetTimerResolution` at kernel start and record the power cost.

### 11. The TLS carrier: § 6 and DR-0003 name different mechanisms

Verdict: needs a decision, cheap either way; acceptable once made.

§ 6 defines the ABI as three words at fixed `%gs` offsets: `%gs:TP` the TCB,
`%gs:TP+8` the canary, `%gs:TP+16` the pointer guard, each one load. That is
carrier C1 or C4 of `spike/gs-thread-pointer/`. DR-0003 chose C3, the word
below `NtTib.StackBase`, reached as `gs:[8]` then a fixed offset: two loads,
5.5 cycles, and no fixed `%gs` offset for GCC's
`-mstack-protector-guard-reg=gs -mstack-protector-guard-offset=` to name. The
two are not the same design, and open question 6 leaves it open. C1 is the
right answer in an ntdll-only process, and the hazard DR-0003 records for it,
an injected DLL drawing the same slot, is removable rather than merely small:
the kernel owns the PEB and can set bit 63 of `TlsBitmap` at start, so no
`TlsAlloc` from any DLL can ever return it. C4 (`ArbitraryUserPointer`) is
declined for a real reason the spike could not see: `LdrLoadDll` writes it
during every DLL load, which an injected module triggers. Take C1, reserve the
bit, and let DR-0003 be reopened by a record that says why.

TLSDESC, `__tls_get_addr`, the static surplus and `PTR_MANGLE` are all inside
glibc and follow the carrier; nothing there is a new risk. The `%fs`
census in risk 7 is where the rest of the answer to question 5 lives.

### 12. Signals into NT waits, and delivery latency

Verdict: acceptable as recorded divergences, with two mechanisms named.

Spike 38 measured asynchronous delivery at 17.9 µs median, 39.9 µs p99, with
the in-kernel flag reliable over 20,000 suspend-and-inspect samples (q4: 9807
delivered, 10193 deferred, no violation) and the rewrite deferred while the
target sits in `ntdll`. Linux delivers in one or two microseconds;
the gap matters to nothing in the acceptance set. Interrupting a thread that
is blocked, rather than spinning, is a discipline the core has to keep: every
wait alertable, `RtlWaitOnAddress` woken by `NtAlertThreadByThreadId`, and
synchronous I/O on a foreign handle (which a Windows parent opened
synchronous, and the kernel cannot reopen) cancelled with
`NtCancelSynchronousIoFile`. Neither call appears in 0011; both are what makes
`EINTR` reach a `read` on `cmd.exe`'s pipe. `SIGSTOP` through
`NtSuspendProcess` is right under N and wrong under H (risk 1). Spike 38 ran
with user shadow stacks off, and a deployment that forces
`PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY` on `lk-host.exe` through Exploit
Protection breaks both the `%rip` rewrite and DR-0030's `iretq` return; that is
a deployment constraint for the presence check in criterion 16, not a design
change.

### 13. The certified N is not the designed N

Verdict: acceptable, provided nobody reads "9/9" as covering the arena.

`Substrate-N.md` records that the conformance run used `VirtualAlloc`,
`VirtualProtect`, `VirtualFree` and `CreateThread`, which is to say inside a
process with `kernel32` loaded. That is a valid realisation of the contract
and it passed the bar. It is not the placeholder arena, the section views, the lazy-commit handler or
the ntdll-only host that § 4 and § 2 describe; those exist as spike 37 and
spike 36, characterised and not certified. The `as_map` that phase 2 will lean
on, with a 64 TB placeholder, 4 KB replacement and a vectored handler, has
never run the suite. The request's "N is certified 9/9" is true of the
contract and not of the mechanism, and the distinction is the one
`Architecture.md`'s last section says is easiest to lose.

### 14. WSL1's reasons, sorted

Verdict: three apply and are accepted by the goal; one is worse here; two do not apply.

Microsoft left the syscall-boundary personality for a VM for reasons that are
public enough to name. File-system performance over NTFS, the per-open cost
through the filter stack and the metadata round trip per file: applies in
full, and risk 9 puts a number on it. The compatibility long tail, where every
program that worked found the next syscall or `ioctl` that did not: applies in
full, bounded only by this proposal's modest bar (rivalling Cygwin) and its
choice to pin 4.18 so that `io_uring`, `clone3` and `openat2` are honestly
absent. Process creation cost: applies and is worse, since WSL1's pico clone
ran in the kernel and this one is a 5 ms user-mode wrapper (risk 4). Coupling
to NT kernel internals that changed under them: does not apply in that form;
this design stands on user-mode-reachable undocumented interfaces, which is
Cygwin's footing and has held for twenty years. Containers, `netns`, cgroups,
GPU and a security boundary: declared non-goals here, so not reasons.

One observation belongs with question 1 rather than with the list. H requires
the hypervisor feature, which is WSL2's deployment prerequisite, and keeps the
whole emulation burden, which is WSL1's engineering cost, and adds a VM exit
per syscall and per first-touch fault on top. Its justification is fidelity
alone: a real FS base, a real `syscall`, page-precise mappings. That is a
real justification. It is also the same fidelity the DBT option in 0011's
alternatives buys without the feature, and the gap between the two has not
been priced.

### 15. `Address-Space.md` describes the veneer, not this design

Verdict: acceptable; one sentence so nobody builds against it.

The document `Architecture.md` sends a reader to for address-space detail
describes `ELF_WINDOW_BASE`, a parent reserving a low window into a suspended
child, Cygwin's `mmap` as the mapping primitive and a `MEM_RESERVE` refused
from `_dll_crt0`. None of that is the design. The address space of record
under N is 0011 § 4 with spike 37's bounds (a 64 TB arena, not the whole user
range), and under H it is whatever risk 1 decides. What survives from the old
document is one argument, that `AT_PAGESZ` reports 4096 whatever the
allocation granularity, and that argument is right.

## Question 1, answered

The nine-call interface is two-implementable where it was tested and N-shaped
where it was not. `thread_interrupt`, the call the bet leans on, has two
realisations that meet one contract (spikes 38 and 41); `as_map`, `as_unmap`
and `as_protect` are page-exact under H and page-exact on this host under N
(spike 37); `tp_set` differs by design and the difference is the substrate
choice, stated honestly. The interface fails in what it omits. It has no
address-space object, so it cannot express a substrate in which one host
process runs many Linux processes; it says nothing about how NT I/O reaches
user memory, which under N is a shortcut and under H is impossible; and the
mechanism it names for `as_clone` under H is a description of a fork performed
by a kernel inside the guest, which this kernel is not. Whether the
two-substrate claim holds therefore turns on one decision the documents have
not taken, H's process topology. If H keeps one host process per Linux
process, the interface holds as written and H's price is measured nowhere:
partition setup per fork, pinned guest memory, and a `WinHvPlatform.dll` that
an ntdll-only host cannot load. If H becomes one host with many partitions,
which is where every working hypervisor-backed sentry has ended up, then § 2,
§ 5 and the interface's signatures are N's, and the claim fails for the design
as written. My recommendation is to keep N, whose remaining risks (the red
zone on a synchronous fault, the fork number, the `syscall` census) are each a
spike of days, and to demote H from "designed" to "an option with four open
measurements" until the topology is chosen and risks 1 and 2 are measured. Do
not drop H's fidelity argument; drop the claim that the second substrate is
not a rewrite, because on the evidence in the tree it is one.

## Missing experiments, collected

Each is named above; this is the list to schedule from, in the order the risks
run.

- Partition create, setup and `WHvMapGpaRange` cost; `WHvCreatePartition`
  from an `RtlCloneUserProcess` child; whether mapped guest memory is pinned
  and whether the host may decommit behind it (risks 1, 2).
- Red-zone pattern across a first-touch fault handled by a vectored handler,
  and across a `SIGSEGV` resumed from its handler (risk 3).
- `RtlCloneUserProcess` from an ntdll-only parent, at 200 VMAs and a
  realistic RSS, profiled against the 521 µs raw clone (risk 4).
- `NtReadFile` into a reserved-uncommitted buffer, to close the zero-copy
  question under N by measurement rather than reading (risk 5).
- The `0f 05` and `%fs` census over BaseOS and AppStream, outside glibc
  (risk 7).
- Spike 37 on Windows 10 1809 and Server 2022 (risk 8).
- Spike 39 extended: rename over open, unlink and truncate of a mapped file
  (risk 9).
- Process-shared futex under contention, and `FUTEX_WAIT` wake latency at
  100 µs and 1 ms timeouts (risk 10).

## Measured since, 2026-09-05

Two of the review's claims were wrong and are corrected here rather than
edited away above.

Risk 3 is refuted. `spike/redzone-sync-fault/` (milestone 47) painted a
leaf's red zone, took a lazy-commit fault and an `int3` through a vectored
handler a thousand times each, and lost nothing; NT places the
`EXCEPTION_RECORD` 568 bytes and the `CONTEXT` 1832 bytes below the
interrupted `%rsp`. The argument from the ABI was wrong about what the
kernel does. DR-0050 stands for both paths.

Risk 1's "the interface has no address-space object" is wrong about the C
form. `substrate/substrate.h` passes a `struct substrate *` to every call and
`as_clone` returns a new one; an instance is an address space with its
threads. The prose omitted it and now states it (0012 § 3). The topology
finding stands and is settled: shape B2, on spikes 42 to 45, with B1 removed
by the one-mapped-partition-per-process ceiling.

Risk 6 is confirmed by `spike/glibc-spawn-clone-flags/` (milestone 46):
`0x4111`, and `vfork` is syscall 58. Proposal 0012 and DR-0099 carry the
contract per substrate.

## Not verified

That NT's exception dispatch writes below the faulting `%rsp` with no gap for
a red zone. Refuted since; see above. That is how the mechanism is understood from the x64 dispatch path
and from the Windows ABI having no red zone to preserve; risk 3's experiment
is the check, and it is listed as a measurement rather than a finding for that
reason.

That `WHvMapGpaRange` pins host pages. It is stated above as the question, not
the answer.

That glibc 2.28's `spawni.c` reports through `args->err` under
`CLONE_VM | CLONE_VFORK`. This is read from the 2.28 source as remembered, not
re-read this session; it should be confirmed against the tree the `sysdeps`
port will patch, which is a one-minute check with the source at hand.

That the disagreement between § 6's single-load TLS ABI and DR-0003's C3 chain
is not already resolved somewhere under `a/` or in a record not read here.
Open question 6 in 0011 says it is open, and this review takes 0011 at its
word.
