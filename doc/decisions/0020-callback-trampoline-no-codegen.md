# DR-0020 -- callback trampolines are fixed per-shape compiled thunks, one live target per shape, no runtime code generation

Status: accepted
Date: 2026-08-30
Deciding: WP-23's implementer; a defensible call, not one of the reserved three
Proposal: none; taken while building WP-23's callback trampolines

## What was decided

A callback the ELF world hands down to Windows is bridged by a fixed, compiled
`ms_abi` trampoline, one per callback shape, that reaches its System V target
through a mutable slot. Registering a callback writes the slot and returns the
trampoline's address; the trampoline is not manufactured per callback. There is
one slot per shape, so one callback of a given shape is live at a time, and a
second registration of that shape rebinds the slot rather than minting a second
trampoline.

No trampoline is generated at run time. The set of trampolines is the set the
source compiles, one per shape the runtime supports.

## Why this and not a per-instance trampoline

The general form of this problem -- an arbitrary number of distinct callbacks,
each a closure over its own target -- is what a thunk-minting allocator like
libffi's solves, and it solves it by writing a small trampoline into memory at
run time and marking that memory executable. That is exactly the self-mapped
anonymous executable memory DR-0000 records as a permanent deployment
constraint: enterprise endpoint protection objects to it, permanently, and the
loader already spends its one allowance of that shape on mapping objects. Buying
a second such surface to multiplex callbacks is a poor trade when the shapes the
platform actually hands down are few and, for the ones that carry a context
argument, not even necessary.

The compiler already emits the crossing correctly for a named `ms_abi` function
that calls a `sysv_abi` target, which spike 3 measured and WP-22's core relies
on. Binding that target through a slot rather than a symbol keeps the trampoline
a fixed compiled entry -- the thing the host holds and can walk -- while letting
it forward a pointer supplied at run time, which is what the seam is for. The
cost is that the slot holds one target, so one callback per shape is live.

## Why one live callback per shape is enough here

The shapes the platform hands down are a small closed set: a comparator, a
thread start routine, an exception filter, and their near relatives. Two of the
three carry a context argument the caller round-trips -- a thread start's
`LPVOID`, a vectored handler's `EXCEPTION_POINTERS` -- through which a real
runtime distinguishes instances without a distinct code address at all. The
comparator is the one shape with no context slot, and a host `qsort` is called
to completion before it returns, so its callback is not concurrently live with
another of its shape in the first place. Where a genuine need for several
simultaneous callbacks of one context-free shape appears, it is answered by
compiling several trampolines for that shape, not by generating code; that is a
change to a number, and it is bounded and auditable in a way run-time codegen is
not.

## What it does not decide

The number of shapes. WP-23 delivers the three the done-condition names;
further shapes are added as the runtime needs to hand them down, each a compiled
trampoline and a slot.

Re-entrancy and threading of the slot. At this width the slot is a plain global
and the test is single-threaded. A real runtime that registers callbacks from
several threads, or re-enters a shape, needs the slot scoped to match -- per
thread, or per registration through a small fixed pool -- and that is the
runtime's to decide when it has threads to decide it against, not this record.

The cross-boundary unwind. Whether a C++ exception or an `RtlUnwindEx` can be
made to travel through the trampoline is DR-0012's reserved question and
WP-43's, not settled here; this record fixes how the trampoline is built and
bound, not what an unwinder may do through it.

## What it costs to reverse

Low. The trampolines are a compiled unit, so adding shapes or compiling several
trampolines per shape is an edit to `callback.c`. Moving to run-time codegen is
the reversal that has weight, and it is the one DR-0000 stands against: it would
be a new record arguing the deployment constraint has changed, not an edit here.

Reversal is a new record pointing back here, not an edit to this one.

## When to reopen this

If the platform ever needs an unbounded or large number of simultaneous
context-free callbacks of one shape -- enough that compiling a trampoline apiece
stops being reasonable -- and DR-0000's self-mapped-memory constraint has been
lifted or bought out for that purpose. Both halves are required: the need, and a
decision that the executable-memory cost is now payable. Absent either, the
fixed compiled set stands.

## Where it is written down

`runtime/core/callback.h` and `runtime/core/callback.c`, whose header comments
state the slot-and-fixed-trampoline shape and the no-codegen reason.
`runtime/core/README.md`, under "The callback trampolines".
`doc/IMPLEMENTATION-PLAN.md`, WP-23, whose delivery note points here.
