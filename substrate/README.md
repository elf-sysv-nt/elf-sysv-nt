# The substrate conformance harness

This directory certifies a substrate against `doc/design/Substrate-Interface.md`.
The interface is the line the core does not cross: above it the syscall table,
the VFS, the process and signal model; below it the two ways user code is
actually run, N on native NT threads and H on a Windows Hypervisor Platform
vCPU. The harness is the runnable half of that contract. It states the nine
calls as C, ships a working substrate that honours them, and holds every one to
the bar the spec fixed before any code was written.

## What is here

`substrate.h` is the contract in C: a vtable a substrate fills in, plus the
Linux-terms types the calls take -- a register state that is the x86-64
`user_regs_struct`, a `prot` set by its Linux values, a backing descriptor.
Each call carries its one-line contract from the spec. Two runtime helpers sit
beside the nine, and are not among them: the thread-pointer read a userland does
through its ABI carrier, and the gate boundary the interrupt latch turns on.

`mock_substrate.c` implements the vtable over NT and Win32 primitives, the mock
the spec's Conformance section describes. `VirtualAlloc`, `VirtualProtect` and
`VirtualFree` realise the address space; native threads with suspend, context
rewrite and resume realise the thread calls; a runtime-owned per-thread word
carries the thread pointer; a `memcpy` behind a fault guard serves the user
copies and returns the fault address rather than crashing.

`conformance.c` is the suite, one group per the nine numbered bars. It runs
against whatever `substrate_create()` hands it, so N and H reuse it unchanged
once they replace the mock. Every group asserts the contract, never the mock's
mechanism.

`run.sh` builds the mock and the suite with the mingw cross compiler, runs them,
and exits zero only when every group passes. `bin/check-substrate-line` is the
separate CI check the spec calls the interface's teeth; the fixtures under
`fixtures/` are what proves it fails on a leak and passes on clean core-shaped
source.

## Running it

    ./run.sh              # build, run, report; -q for quiet, -k to keep the build
    ./run.sh --help       # the full option set

    python3 ../../bin/check-substrate-line PATH...   # scan core sources for a leak

The check is a clean no-op when handed no paths, which is its normal state until
the first core source lands.

## What the harness is not

The mock is not a real substrate, and does not try to be. Its job is to prove
the contract is coherent and the suite is real before N or H exists; a suite no
implementation has ever passed is a wish rather than a bar. Where it stands in
for mechanism a real substrate owns, it says so at the site.

`as_clone` is the clearest of these. The mock is one process, so it cannot give
a child the parent's addresses; it snapshots each mapping into a private copy the
child's user copies read and write, which satisfies the contract the suite tests
-- the child reads the parent's pre-clone contents, and a later write on either
side stays private -- without being a process fork. A real N clone is
`RtlCloneUserProcess` (spike 35); H duplicates guest page tables copy-on-write.
The fault guard is a vectored exception handler with `__builtin_setjmp`, because
this compiler has no MSVC `__try`/`__except`; the observable is the one the spec
asks for, reached the way GCC allows.

N and H each replace the mock and must pass this identical suite. A test that
needed editing to admit one of them would have been measuring mechanism, and
the spec says to rewrite it to measure the contract instead.
