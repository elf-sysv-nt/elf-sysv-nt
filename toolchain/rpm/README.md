# Build macros

WP-16. Cheap by design: every package in the set rebuilds anyway, so the target
mandates land once here instead of in four hundred spec files.

`macros.elfsysvnt` carries the triple, the sysroot paths, the tool names in
full, and `-mno-red-zone`. WP-63 installs it as
`/usr/lib/rpm/macros.d/macros.elfsysvnt`, reseeded from this copy on every run.

The exit criterion has two halves and they are unequal. A package that names no
flags getting all of them is the easy half, and it falls out of `%optflags`.
The ledger is the other half and it is the one worth the section below.

## Why -mno-red-zone appears twice

The compiler already defaults it. `gcc/config/i386/elfsysvnt.h` sets
`TARGET_SUBTARGET_DEFAULT` to `MASK_NO_RED_ZONE`, so a caller who passes
nothing gets it, which is what makes it a mandate rather than an option.

Repeating it in `%optflags` is not belt and braces so much as evidence. It puts
the flag in every build log, where somebody debugging a corrupted stack in
month nine will actually look, and where its absence would otherwise be
indistinguishable from a compiler that had quietly stopped defaulting it.

`-mred-zone` still turns it back on, deliberately. WP-43 may retire the flag
entirely if a signal delivery that reserves the 128 bytes proves cheap enough,
and a target that refused the option outright would have to be rebuilt to find
out.

## The ledger, which is the residual risk

A compiler flag governs what the compiler emits. Hand-written assembly is
outside it, permanently, and spike 3 measured Cygwin's delivery taking the word
at `%rsp-8` on every single delivery. A routine that keeps a value below the
stack pointer is corrupted at an unpredictable later date in a package nobody
was looking at, and there is no flag that reaches it.

`bin/asm-ledger` bounds who has to be read. It turns a source set into the
packages carrying `.S` or `.s` files or inline `asm`, and within those, flags
the ones whose assembly addresses memory below the stack pointer. That third
column is the one to act on.

It is a ledger rather than a verdict. Deciding whether a given routine actually
depends on the red zone means reading the routine, and no regular expression
does that. What the tool does is turn 2893 packages into a few dozen.

The pattern is deliberately loose, because the costs are asymmetric: a false
positive costs somebody a minute reading a routine, and a false negative costs
a corrupted stack in a package nobody was looking at.

## Not verified

The ledger has not been run over the el8 set. The source dump the
triple-fidelity spike used is gone from this machine, and refetching 2893
source packages is hours rather than minutes. The tool is verified against a
constructed tree and against `flac`, which is to say it works and has not yet
been pointed at the thing it exists for. Running it is the first thing WP-16
owes once a source set is unpacked again.

That the below-rsp pattern catches Intel-syntax assembly reliably. It has an
alternation for `[rsp - N]` and nothing in the el8 set has exercised it. Most
GNU-toolchain assembly is AT&T, which is what the tested arm covers.

That `%optflags` matches what el8 actually builds with. It was written from
RHEL 8's documented flags and not diffed against a vendor `redhat-rpm-config`,
which is a cheap check nobody has run.

That CET can be disabled this way. Spike 2 established the PE stub opts out of
shadow stacks and Control Flow Guard for the process; `-fcf-protection=none`
here is the compiler side of the same decision and has not been tested
together with it.
