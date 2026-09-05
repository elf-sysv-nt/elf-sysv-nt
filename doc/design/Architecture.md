# Architecture

What this system is, in the present tense. A reader who wants to know why a
thing is the way it is goes to `doc/design/decisions/`; a reader who wants to
know what is built goes to `doc/status/`. Proposal 0011 is the design of
record, and it is long, complete and accepted: this document is the map over
it, not a summary of it. Where 0011 settles something in detail, the section
here says what the shape is and points at the section that carries the detail.

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

One kernel, two substrates, one interface between them.

The **core** is the Linux personality: the syscall table, the process and
thread model, signals, the VMA tree, the VFS, and the descriptor layer. It is
written once and knows nothing about NT. `bin/check-substrate-line` enforces
that mechanically — nothing above the line may name `Nt*`, `WHv*`, `CONTEXT`,
an NT `HANDLE` type, or dereference a user pointer — and it walks `core/` on
every gate run.

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
unmodified, at about five microseconds a syscall against N's one. It is
designed, its nine calls are each backed by a landed spike, and it is not
built. The two exist because different deployments need different ones, and
the choice is not always the fast one: a client environment with nested
virtualisation outside support forces N whatever the measurements say.

## The gate, and what has run

Under N there is no `syscall` trap, so the kernel publishes a gate address in
the auxiliary vector as `AT_SYSINFO` and user code reaches it with a plain
`call`. The register convention is the kernel's: number in `%rax`, arguments in
`%rdi %rsi %rdx %r10 %r8 %r9`, result in `%rax`, with `%rcx` and `%r11`
clobbered. The gate switches to a per-thread kernel stack before running any
core frame, captures the call, and restores the user state on the way out.

Phase 1 of the core is built and runs: a static ELF written to that ABI is
mapped through the substrate, entered on the psABI initial stack with its
auxiliary vector, calls `write` and `exit_group`, and prints `hello` before
exiting 0 — checked against the same logical program on a Rocky 8 oracle.
`doc/design/Core-Phase1.md` records it. Every other syscall number returns
`-ENOSYS`, which is what a 4.18 kernel does for what it lacks.

Nothing else of the core exists yet. The VFS, the descriptor layer, fork,
signals and the rest are designed in 0011 and unbuilt, and this document says
so rather than describing them as though they were here.

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
address at about five cycles, and the one in use is the carrier the spike
called C3. The ABI above it is glibc's own: `%gs:TP` holds the TCB pointer,
`%gs:TP+8` the stack-protector canary, `%gs:TP+16` the pointer guard, which is
what `tls.h`, `-fstack-protector` and `PTR_MANGLE` read. The kernel sets it
through the interface's `tp_set`, and `arch_prctl(ARCH_SET_FS)` returns
`EINVAL`, because a program built for this substrate never asks.

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

Settled by: DR-0003, DR-0021, DR-0024, DR-0063.

## The loader

The kernel maps a static image itself; a dynamic one is handed to a dynamic
loader that runs above the kernel like any other userland code.

The loader's cache is this project's own format rather than glibc's. Reading
glibc's would tie the platform to a layout that changes for reasons that have
nothing to do with it, and the cache is small enough that owning it is cheaper
than tracking it.

Relocation types the platform will never emit are certified against real vendor
objects rather than assumed absent, which is the only way to know that the set
a loader implements covers the set a distribution ships.

A weak undefined symbol is not a demand on the runtime. It resolves to zero and
the program tests it, which is what the ABI says and what a program that ships
one expects.

The debugger rendezvous is the standard link map, so a host debugger attaching
to a process can walk the loaded objects without the platform inventing a
protocol for it.

`exec` has one classifier, and an interpreter chain is followed at most four
hops before it is refused, which is a limit rather than a recursion.

Settled by: DR-0011, DR-0016, DR-0022, DR-0027, DR-0073.

## Process shape

`fork` clones the address space. Under N that is `RtlCloneUserProcess`, the
fork-shaped wrapper over `NtCreateUserProcess`, which returns
`STATUS_PROCESS_CLONED` in a child on a live thread; the raw path of creating a
process from a null section clones the address space but cannot start a thread
in it. What crosses the fork is enumerated rather than assumed, and the child
checks what it received rather than trusting that it arrived.

A signal is delivered by building a frame on the target's stack and resuming
it there. The frame is built below the red zone, so a handler that returns into
code relying on those 128 bytes finds them intact, and a thread already inside
a kernel wait takes the delivery when the wait returns rather than in the
middle of it.

A fatal signal leaves an ELF core, written by the kernel from the VMA tree and
the thread state it already holds.

Settled by: DR-0029, DR-0030, DR-0033.

## The address space

`doc/design/Address-Space.md` carries this in detail. The shape: the kernel
reserves the user range as a placeholder and replaces pieces of it with section
views, committing lazily through a fault handler. A single placeholder holds at
half the user range, and the practical claim is the half-range one; the literal
whole-range claim does not hold on this kernel.

The alignment invariant the design builds against is the documented 64 KB —
`p_vaddr ≡ p_offset (mod 64 KB)` — even though this host's kernel accepts a
4 KB split and a 4 KB file-backed view. Building against the coarser rule and
probing for the finer one costs little and does not rest a shipped artifact on
one machine's measurement.

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
the interrupt call that the two-substrate bet depends on, but no core has sat
on it.

Everything in "The loader" and "Process shape" above is designed and recorded,
and none of it is built. Those sections describe what the records settle, which
is not the same as describing what runs. The distinction is the one this
document exists to keep, and it is easier to lose here than anywhere else in
the tree.
