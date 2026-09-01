# DR-XXXX — the loader runs a crossed image's DT_INIT chain before entry, across the ABI boundary

Status: accepted
Date: 2026-09-01
Deciding: the build worker, on WP-56
Proposal: none; this is the step dyn_exec.h and the exec README already named as
the one staged behind the link.

## What was decided

After the dynamic crossing (DR-0058) relocates a main image against its runtime
and before the stub enters it at `e_entry`, the loader runs the image's
initializers: DT_PREINIT_ARRAY, then DT_INIT, then DT_INIT_ARRAY, each array in
forward order, a null or all-ones entry skipped as the linker's padding. This is
`dyn_init_run` in `loader/exec/dyn_init.c`, called from the stub's dynamic
branch. The order is the one the ABI fixes and the one WP-38's `dl_run_init`
already runs for the dl graph.

An el8 program's constructors run before `main`, called by the C runtime the
program is linked with. The images this route runs have no startup file of their
own — the crossing enters a raw `e_entry` — so nothing would run them unless the
loader does, exactly as `ld-linux` runs the initializers of a program whose
startup never gets the chance. A program entered with its constructors unrun is
a program whose globals are unbuilt; bzip2's own `make test`, the acceptance the
WP carries, cannot pass without this.

## Why it reads the pair, not the dl graph

`dl_run_init` already walks DT_INIT and the arrays, but over the `dl_state`
object graph — the full dynamic-linker model, with a dependency-ordered set of
objects. The exec crossing holds one object, the main image, as the
`elf_parsed` and `elf_mapping` the parse and the placement produced; it never
builds a `dl_state`. So `dyn_init_run` reads that pair directly, finding the
dynamic array the way WP-34's engine does — a validated file offset turned into
a link vaddr through the PT_LOAD that backs it, then biased into place — and
runs the same tags in the same order. It mirrors `dl_run_init`'s order rather
than composing its graph, because dragging the graph model into the stub to run
one object's initializers would be the heavier dependency, not the lighter one.
When a later package gives this route a full dependency set, the order those
objects run in is `dl_run_init`'s to decide; one main image has no such order to
decide.

## Why the call crosses an ABI boundary

The stub is a Windows PE, built for the Microsoft x64 calling convention: its
arguments arrive in `%rcx/%rdx/%r8`, and `%rsi` and `%rdi` are registers a
callee must preserve. A System V initializer takes its arguments in
`%rdi/%rsi/%rdx` and treats `%rsi` and `%rdi` as scratch. Called as an ordinary
function pointer, the initializer reads its `argc` from a register the stub
never set and clobbers two registers the stub trusts across the call; the
damage surfaces later, as the stub misbehaves after the initializer has already
returned. It was measured here first: the specimen's initializer saw `argc` as
zero though the stack carried one, and on a second shape the stub lost its way
between the initializer's return and the next statement and the process left
with a status nothing wrote.

`enter.S` bridges the same two ABIs by hand for the one-way entry, because a
stack switch is not expressible in C. An initializer call is expressible: it is
an ordinary call that returns, so the bridge is the `sysv_abi` function
attribute on the pointer type, which makes the compiler marshal the arguments
and preserve the Microsoft-callee-saved registers around the call. The attribute
is the whole of the fix; the corruption above is what its absence costs.

## What this does not settle

This runs the main image's initializers. Running the runtime object's own
DT_INIT chain, and the finalizers on the way out, belong to the packages that
give this route more than one object and a way back; a single crossed program
with a bare runtime specimen reaches its entry with its own constructors run,
which is what this step is. The acceptance green — bzip2's `make test` under the
real veneer — waits on that veneer and the syscall surface, not on this.
