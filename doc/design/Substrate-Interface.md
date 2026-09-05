
The nine calls the core makes to run user code, and the contract each one owes,
so that the core is written once and a substrate is an implementation rather
than a rewrite. Proposal 0011 § 3 names the interface and sizes it; proposal
0012 § 3 names the object the calls act on; this is the protocol.
`doc/design/Architecture.md` § The shape of the system places it, and
§ The address space is the VMA protocol that sits just above `as_map`.

The whole value of the interface is a line the core does not cross. Above it
sit the syscall table, the VFS, the process and signal model, epoll, sockets,
IPC — everything that is Linux rather than Windows, written once. Below it sit
the two ways user code is actually run: N, native NT threads reached through a
gate; H, ring-3 on a Windows Hypervisor Platform vCPU. The core names a thread
by its Linux tid and a range by its address, and never learns which substrate
is under it. A substrate that cannot honour a call's contract without the core
knowing is a leak, and a leak is where "the second is not a rewrite" stops
being true.

## The object

Every call takes a substrate instance, `struct substrate *s` in the C form
(`substrate/substrate.h`), and an instance is one address space together with
the threads that run in it. The core holds one per Linux process: the first
comes from `substrate_create` at the first `exec`, every later one from
`as_clone` at `fork`, and `execve` reuses the instance with its ranges
dropped. A tid names a thread within its instance and nowhere else. Under N an
instance is an NT process, so `as_clone` produces a child process; under H an
instance is a page-table root in the one kernel process, and the partition
that runs every root is state shared by all instances, which no call names.
That is why the interface needs no address-space argument: the receiver is
the address space, and the C form has always said so.

## The nine calls

Signatures are sketched, not frozen; the contract is the load-bearing part. A
`tid` is the Linux thread id the core assigns; a `range` is `[start, start+len)`
in the user address space; `prot` is the Linux `PROT_*` set.

    as_map(vma, backing, offset, prot)     realise a VMA, or part of one
    as_unmap(range)                        drop a range
    as_protect(range, prot)                change protection, page granularity
    as_clone() -> child                    duplicate the address space (fork)
    thread_start(tid, ctx, tls)            run user code from a register state
    thread_interrupt(tid)                  force a thread into the kernel, soon
    thread_context(tid) -> ctx             read a thread's user registers
    set_context(tid, ctx)                  replace a thread's user registers
    tp_set(tid, base)                      set the thread pointer
    user_copy_in(dst, uaddr, len)          copy from user memory into the kernel
    user_copy_out(uaddr, src, len)         copy from the kernel into user memory

`thread_context` and `set_context` are one call in each direction and are
counted as one; `user_copy_in` and `user_copy_out` likewise. Nine, as § 3 says.

## The contract, call by call

**`as_map(vma, backing, offset, prot)`.** After it returns success, a load or
store within the VMA that `prot` permits reads or writes the backing, and the
range appears in the core's VMA tree as the core recorded it — the substrate
does not keep its own map of record (Architecture.md § The address space: the
core keeps the map, the substrate realises edits to it). Backing is one of:
anonymous zero-fill; a file at `offset`; a shared section; the vDSO; the
stack. A first touch of an
anonymous page reads as zero without the core having written it. The call is
idempotent against the core's tree: re-realising a range the tree already
describes is not an error, because `as_clone` and demand paging both re-issue
it. Failure leaves the range exactly as it was — no partial mapping survives a
failed call.

**`as_unmap(range)`.** After success the range faults on access until re-mapped,
and it no longer appears in `/proc/self/maps`. A range spanning several VMAs, or
part of one, is dropped exactly to the byte; the surrounding VMAs are untouched.

**`as_protect(range, prot)`.** Changes protection at page granularity with no
other effect; contents survive. This is the one call the substrate may refuse
to make byte-exact: N realises protection on NT pages and cannot protect below
a page, which is why the core keeps protection in the VMA tree and only asks the
substrate to realise the page-rounded envelope. H protects at guest-page
granularity through its own page tables and is exact.

**`as_clone() -> child`.** Produces a child address space whose contents equal
the parent's at the call, with copy-on-write semantics: a later write on either
side is private to that side. This is `fork`'s address-space half; the core
rebuilds the child's VMA tree from its own description and expects `as_clone` to
have made the pages match. N realises it by the executive clone spike 35 proved
(`RtlCloneUserProcess`), and the child instance lives in a new NT process. H
realises it by copying the parent's page-table tree in the kernel process,
clearing the write bit in every copy-on-write-eligible leaf of both trees and
counting the frames, and resolving the first write on either side when the
guest's `#PF` reaches the kernel as an exception exit (spike 44: the exit
carries the fault address and `%rip` at the store, the stale translation
needs no flush, and the copy costs what the tables cost). The child instance
is a new root in the same process.

**`thread_start(tid, ctx, tls)`.** Begins executing user code for `tid` from the
register state `ctx`, with the thread pointer at `tls`. Under N this is an NT
thread entering the gate-return path at `ctx`; under H a vCPU entered with those
registers. The core supplies the full register state; the substrate does not
invent any of it.

**`thread_interrupt(tid)` — the call the whole bet leans on.** Forces `tid` out
of user code and into the kernel *soon*, from another thread, whether `tid` is
spinning in user mode or blocked. "Soon" is bounded, not immediate: a thread
already inside the substrate's own kernel path finishes it first (N defers a
hijack while the thread is in `ntdll`; spike 38). The interruption is resumable
— after it, `set_context` may rewrite the thread's registers (to deliver a
signal frame) and `thread_start`/re-entry resumes it — so it is an interrupt,
not a teardown. An interrupt raised while `tid` is momentarily not interruptible
is latched, not lost: the next interruptible point takes it, exactly once.
Spike 38 proved this on N by suspend-and-rewrite; spike 41 proved it on H by
`WHvCancelRunVirtualProcessor`, register injection, and WHP's own latching. The
two realisations differ entirely below the line and are identical above it,
which is the interface working.

**`thread_context(tid)` / `set_context(tid, ctx)`.** Read and replace the user
register state of a thread that is not currently running user code — after
`thread_interrupt`, or at a gate/exit boundary. The register set is the Linux
`user_regs_struct` the core reasons about; the substrate maps it to NT's
`CONTEXT` (N) or WHP registers (H). `set_context` honours the red zone: the core
builds a signal frame 128 bytes below the interrupted `%rsp` (DR-0030, DR-0050),
and the substrate must not clobber that gap (spikes 38, 41 both check it).

**`tp_set(tid, base)`.** Sets the thread pointer for `tid`. Under N the FS base
does not survive a deschedule (spike 1, `fs-base-persistence`), so the pointer
is a runtime-owned word reached through `%gs` per DR-0003, and `tp_set` writes
that word; the toolchain is built against it (§ 3, § 4.16). Under H the guest
has a real FS base and `tp_set` writes the guest MSR — which is why H runs el8's
shipped binaries unmodified and N does not. This is the one call whose two
realisations have different *capability*, not just different mechanism, and the
difference is the substrate choice.

**`user_copy_in` / `user_copy_out`.** The only way the core touches user memory.
It never dereferences a user address directly, because under H user memory is
guest-physical the core reaches through `WHvMapGpaRange` and under N it is a
range that may fault on an uncommitted page the core must handle. A copy that
hits an unmapped or wrongly-protected user address fails with the fault
address, and the core turns that into `EFAULT` — it never crashes the kernel on
a bad user pointer. This is the call that makes the core substrate-blind about
where user memory physically is.

## Invariants the core relies on, and must not violate

The interface is two-sided: the substrate owes the contracts above, and the
core owes these, or the abstraction leaks from the top.

1. The core keeps the address space of record. Every `mmap`/`munmap`/`mprotect`
   edits the VMA tree first and asks the substrate to realise it second
   (Architecture.md § The address space). The core never reads back a map
   from the substrate.
2. The core touches user memory only through `user_copy_*`. No direct
   dereference of a user address anywhere above the line — that is what lets H
   put user memory in guest-physical space the core cannot name directly.
3. The core names threads by tid and never by an NT `HANDLE` or a WHP vCPU
   index. A substrate handle never appears above the line.
4. The core assumes `thread_interrupt` is "soon", never "now". Code that must
   run before a thread makes progress uses the interrupt-then-context sequence,
   not an assumption of synchronous stop.
5. The core builds register state and signal frames in Linux terms and hands
   whole states across; it never assembles an NT `CONTEXT` or a WHP register
   block itself.

A CI check enforces the mechanical half of these: nothing above the substrate
line may name `Nt*`, `WHv*`, `CONTEXT`, an NT `HANDLE` type, or a raw user-
pointer dereference. `bin/check-substrate-line` (added with this document) walks
the core sources and fails on such a symbol, the same way the project's other
checkers fail on an ungoverned edit. That check is the interface's teeth: a leak
is a red build, not a discovery during the second substrate.

## Conformance

A substrate is conformant when it passes the suite in `substrate/`, one
group per call, each asserting the contract above rather than an implementation
detail. The suite is written against the interface, not against N or H, so both
run the same tests and a third substrate (the DBT option § 4 names) would too.

The bar, per call, stated so a test can check it:

1. `as_map` — a mapped anonymous page reads zero on first touch; a file
   backing reads the file at `offset`; a failed map leaves nothing mapped.
2. `as_unmap` — an unmapped range faults; a partial unmap leaves neighbours
   intact to the byte.
3. `as_protect` — a page dropped to `PROT_READ` faults on write and still
   reads; contents survive the change.
4. `as_clone` — the child reads the parent's pre-clone contents; a post-clone
   write on either side is private to it.
5. `thread_start` — a thread runs from a supplied register state and reaches
   a known instruction.
6. `thread_interrupt` — a spinning thread is forced out and resumes intact; an
   interrupt during a non-interruptible window is latched exactly once; the red
   zone survives a `set_context` after it. (This is spikes 38 and 41 promoted
   from characterization to conformance.)
7. `thread_context`/`set_context` — a round-trip preserves every register; a
   rewritten `%rip` takes effect on resume.
8. `tp_set` — a thread reads its own thread pointer back after a deschedule
   (the property spike 1 measured N failing at the FS base and DR-0003's `%gs`
   carrier passing).
9. `user_copy_*` — a good copy moves the bytes; a copy to an unmapped user
   address fails with the fault address and does not crash the kernel.

The suite ships with a mock substrate — NT threads, `VirtualAlloc`, suspend and
context rewrite, `memcpy` behind a fault guard — that passes every group. Its
job is not to be a real substrate but to prove the contract is coherent and the
tests are real before N or H exists: a suite no implementation has ever passed
is a wish. N and H each replace the mock and must pass the identical suite,
unchanged; a test that needs editing to admit a substrate is a test that was
measuring mechanism, and is rewritten to measure the contract.

## What differs, and is allowed to

Two calls have realisations that differ in capability rather than mechanism,
and the difference is the substrate choice, recorded so nobody reads it as a
defect:

- `tp_set`: N needs a rebuilt userland (the `%gs` carrier); H does not (a real
  FS base). This is why H runs shipped el8 binaries and N runs a rebuilt el8.
- `thread_interrupt` cost: N pays a suspend/resume (~18 us, spike 38); H pays a
  vCPU exit (~4 us, spike 41). Both meet the contract; the number is a
  per-substrate property, not a conformance criterion.

Everything else is identical above the line by construction, which is the
proposition this document exists to keep true.

## Not settled here

The interface is fixed at nine calls by § 3 and proven two-implementable at its
hardest call (spike 41). Two questions it deliberately does not close, each its
own later measurement:

- Multi-vCPU exit cost under H, when many Linux threads exit at once. Spike 40
  measured one; the interface does not assume a number, but the H realisation's
  scaling is unmeasured.
- The N `thread_interrupt` under CET with user shadow stacks on. Spike 38 ran
  with them off; the contract holds in principle either way, but the `%rip`
  rewrite against an enabled shadow stack is unmeasured.

Neither reopens the interface; both are properties of a realisation, to be
measured when that realisation is built.
