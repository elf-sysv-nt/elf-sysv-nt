# DR-0012 -- host-facing entry points are ms_abi with compiler unwind data; System V frames carry none

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-22's implementer; a defensible call, not one of the reserved three
Proposal: none; taken while building WP-22's host-facing core against a measurement

## What was decided

Every entry point Windows calls into the runtime is an `ms_abi` function that
carries the SEH unwind data the compiler emits, and the runtime relies on that
compiler-emitted `.pdata`/`.xdata` rather than hand-authoring unwind records.
System V frames beneath the entry point carry no unwind record the host
recognizes, and that is deliberate: no host unwinder is ever pointed through a
System V frame. The convention boundary and the unwind boundary are the same
line, and it runs through the entry point.

## What was measured

On `x86_64-pc-cygwin` gcc 7.4.0, built `-mno-red-zone`, asked through the host's
own `RtlLookupFunctionEntry` -- the function the exception dispatcher calls to
find a frame's unwind record:

  - An `ms_abi` function returns a `RUNTIME_FUNCTION`. The host can walk it.
  - A `sysv_abi` function returns nothing. gcc emits no host-format unwind
    record for it, so the host treats the frame as a leaf and cannot walk it.

And, separately, that this does not break delivery: a fault taken directly in a
`sysv_abi` frame that carries no record still reaches Cygwin as SIGSEGV and
returns, because Cygwin's delivery restores a saved context rather than
unwinding the intervening frames. The measurement is in `runtime/core/t`, whose
`unwind-present`, `unwind-seam` and `fault-direct-sysv` cases assert all three.

## Why this and not hand-authored unwind data

The alternative is to write `.xdata` for the System V frames so the host could
walk them, which would let the boundary be crossed by the host's own unwinder
rather than fenced off from it. It is a large hand-verified surface -- one
record per frame shape, correct against the actual prologue -- bought to enable
a thing the design does not want: a Windows unwinder walking ELF-world frames.
The project's rule is Wine's, that DWARF and `.eh_frame` stay in the ELF world,
SEH stays in the host-facing core, and the only place they meet is a trampoline
that knows to stop. Fencing the host unwinder off from System V frames is that
rule expressed in what the frames carry, and the compiler already produces
exactly the split the rule wants: records for the `ms_abi` side, none for the
System V side. Relying on the compiler's output is less code and less to get
wrong than authoring records whose purpose is to be walked past.

The seam is safe because nothing asks the host to cross it. The fault path
reaches the runtime through Cygwin's delivery, which does not `RtlUnwindEx`
through the System V frames; it restores a context saved before them. A path
that did need to unwind across the boundary -- a C++ exception thrown in the
ELF world and caught in the host, say -- is WP-23's and WP-43's to build with a
trampoline at the seam, and this record is the constraint it builds against,
not a thing it may quietly violate.

## What it does not decide

The cross-boundary unwind. Whether a C++ exception or an `RtlUnwindEx` can be
made to cross the boundary at all, and what the trampoline that lets it must
do, is WP-23's and WP-43's. This record fixes only that the host unwinder does
not walk a raw System V frame and that the entry points it does walk carry
compiler-emitted records.

The signal frame. What `siginfo_t` and `ucontext_t` the ELF world is handed,
and the red-zone reservation at the delivery site, are WP-43's. This record is
about which frames the host can walk, not about the frame the handler reads.

Hand-written assembly's unwind data. A compiler flag and a compiler's records
reach only what the compiler emits. Hand-written `ms_abi` code that Windows may
unwind through needs its own `.seh_*` directives, and the ledger of that
residue is WP-16's, the same residue `-mno-red-zone` leaves.

## What it costs to reverse

Low to moderate. Relying on compiler records is the absence of a mechanism
rather than one, so reversing means adding hand-authored `.xdata` for System V
frames and a trampoline that walks them -- new work, but it does not unpick
anything here, because the entry points stay `ms_abi` either way. What a
reversal changes is the constraint WP-23 and WP-43 build against: today they may
assume the host never walks a System V frame, and a reversal is a new record
telling them it now can, at the seam, under stated conditions.

Reversal is a new record pointing back here, not an edit to this one.

## When to reopen this

If a later gcc, or the cross toolchain's gcc 13.3.0 once the runtime links
against host objects it builds, emits host-format unwind records for `sysv_abi`
functions, the seam's second half stops holding by construction and this record
is reopened against that measurement. The test's `unwind-seam` case is the
tripwire: it fails the day a System V core starts carrying a `RUNTIME_FUNCTION`.

Reopen also if a real cross-boundary unwind turns out to be needed on a path
that cannot route through a trampoline -- if the host's own dispatch, rather
than Cygwin's delivery, has to walk from an `ms_abi` frame down into System V
frames to reach a handler. The premise here is that every such crossing has a
trampoline it can be made to pass through; a crossing that does not is a
different scope.

## Where it is written down

`runtime/core/README.md`, under "The unwind seam". `runtime/core/core.h`, whose
unwind contract states it. `runtime/core/t/core_test.c`, whose `unwind-present`
and `unwind-seam` cases measure it. `doc/IMPLEMENTATION-PLAN.md`, WP-22, whose
delivery note points here.
