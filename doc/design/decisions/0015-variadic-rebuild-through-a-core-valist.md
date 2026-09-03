# DR-0015 -- the variadic veneer rebuilds a Microsoft va_list and repasses through a va_list core

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-24's implementer; a defensible call, not one of the reserved three
Proposal: none; taken while building WP-24's veneer

## What was decided

A variadic export walks its own System V `va_list`, builds a Microsoft
`va_list` from it, and calls the runtime core's `va_list`-taking form. For the
format-driven families -- `printf`, `scanf`, and the reporters -- the rebuild is
driven by the format string, in `sv2ms.c`; the entry point then calls
`__core_vfprintf`, `__core_vsnprintf`, `__core_vfscanf`, and so on, the
Microsoft-ABI cores declared in `core.h`. For the prototype-driven variadics --
exec, spawn, `open`, `fcntl`, the IPC ctls -- there is no rebuild: the trailing
arguments are walked for the exact types the fixed signature names and passed to
a fixed-arity core.

The veneer names only the core, never a public export, and the core's contract
is a `va_list`, not an ellipsis.

## Why a rebuild and not a forward

WP-21's down-call is a signature-agnostic tail jump because it repacks nothing.
A variadic up-call is the opposite: it must repack everything, because the two
`va_list` types disagree at twenty-four bytes against eight and a System V list
handed to a Microsoft reader yields the descriptor header where the first
argument should be. Spike 3 measured exactly that. So there is no forward to
give; the list has to be rebuilt.

## Why the core takes a va_list and not an ellipsis

Having walked the System V list, the repass still cannot target a `...` callee:
C fixes a variadic call's argument list at compile time, and there is no
portable way to splat a run-time-determined sequence of values into one. The
core must therefore accept the rebuilt list as a `va_list` parameter. This also
matches how a libc is already built -- the `printf` family funnels through
`vfprintf`, the `scanf` family through `vfscanf` -- so the core the veneer needs
is the core the runtime already has, and `core.h` is a thin naming of it rather
than a new surface.

## Why the rebuild is driven by the format

A System V variadic argument lives in the integer register file or the floating
one, and only the format says which. Nothing generic recovers that from the
bytes: a blind copy of the register save areas cannot know whether a given slot
is an integer or a double, and would place half the arguments wrong. So the
rebuild parses the format and pulls each argument at its named type, which is
what tells `__builtin_va_arg` which save area to read. The cost is that the
veneer carries a printf/scanf format walker; the benefit is that it is correct
rather than approximately correct, and that the walker is one place, shared by
the whole family, rather than a per-function argument list.

## What it does not decide

The core's implementation. `core.h` fixes the shape WP-22 must provide -- a
`va_list`-taking, Microsoft-ABI function per family -- not how the re-faced
runtime satisfies it. The test's `t/core.c` is a stand-in, not the record.

`long double`. The value representation differs between the ABIs, so a slot copy
cannot carry it; the walk narrows it to `double`. Whether the runtime needs
exact `long double` across this seam, and at what cost, is left open and noted
in the README.

The prototype-driven twelve beyond the two worked. `open` and `execl` fix the
pattern; wiring the rest into real cores is WP-22's as it builds them.

## What it costs to reverse

Low for the mechanism, moderate for the contract. The format walker and the
generator are regenerated from `variadic-exports.tsv`, so changing how a wrapper
rebuilds the list is an edit to `sv2ms.c` or `gen-veneer.sh` and a rerun. The
`va_list`-core contract in `core.h` is what WP-22 will build against; changing it
to, say, an ellipsis core -- which cannot work, but stands as the shape of a
reversal -- would revise every core WP-22 writes. That coupling is why the
contract is settled now, before WP-22 depends on it.

Reversal is a new record pointing back here, not an edit to this one.

## Where it is written down

`runtime/varargs/README.md`, which carries the pattern and the two shapes.
`runtime/varargs/sv2ms.c` and `runtime/varargs/core.h`, whose header comments
state the rebuild and the contract. `doc/IMPLEMENTATION-PLAN.md`, WP-24, whose
delivery note points here.
