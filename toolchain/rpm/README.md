# Build macros

WP-16. Cheap by design: every package in the set rebuilds anyway, so the target
mandates land once here instead of in four hundred spec files.

`macros.elfsysvnt` carries the triple, the sysroot paths, and the tool names in
full. WP-63 installs it as
`/usr/lib/rpm/macros.d/macros.elfsysvnt`, reseeded from this copy on every run.

The exit criterion has two halves and they are unequal. A package that names no
flags getting all of them is the easy half, and it falls out of `%optflags`.
The ledger is the other half and it is the one worth the section below.

## -mno-red-zone is retired

The flag once appeared twice -- defaulted by `gcc/config/i386/elfsysvnt.h` and
repeated in `%optflags` as evidence in every build log -- because DR-0006
carried it as scaffolding until its replacement landed. The replacement is in:
WP-43 built and certified the delivery-site repair, which reserves the psABI's
128 bytes before Cygwin builds a handler frame, and its cost measured
negligible. So the flag is retired at both points, the compiler defaults to the
red zone like any x86-64 target, and a package's leaves use it as System V code
does. `-mno-red-zone` stays selectable for a package that needs it; it is no
longer forced. The decision superseding DR-0006 records it.

## Where the flags come from

`%optflags` is expanded from `redhat-rpm-config-131-1.el8`, not from RHEL 8's
documentation. It carried the documented flags until 2026-09-02, and the two
disagreed in six terms — among them `-fPIE`, whose absence is why the
acceptance harness produced an `ET_EXEC` at `0x400000` that el8 does not ship,
and why WP-56 spent a session parked on a placement conflict el8 never has.

Red Hat spells its hardening as `-specs=` arguments naming files inside
`redhat-rpm-config`; `redhat-hardened-cc1` injects `-fPIE` and
`redhat-hardened-ld` injects `-pie`. Those files are Red Hat's rpm
configuration and are not in this sysroot, so the flags they inject are
written out here instead. That is S2 in `doc/substitutions.md`, and what
closes it is a build under real rpm macros on a real el8 root.

`spike/vendor-hardened-build/expand-flags.py` is the check, and it runs as
part of that spike's regeneration. It resolves both macro chains — the
vendor's and this one's — and diffs the terms that reach the compiler, with
each `-specs=` argument replaced by what it injects, so a difference in
spelling does not read as a difference in flags. The transcript's
`cflags_absent_from_ours` line is what catches the next drift; it reads
`-fcf-protection -fplugin=annobin` today, which is the pair of deliberate
divergences the Not verified section below argues for. Anything else
appearing on that line is a bug, not a decision.

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

That CET can be disabled this way. Spike 2 established the PE stub opts out of
shadow stacks and Control Flow Guard for the process; `-fcf-protection=none`
here is the compiler side of the same decision and has not been tested
together with it. It is one of the two terms left over once `%optflags` was
expanded against a real `redhat-rpm-config` (below).

That dropping annobin costs nothing that matters. It is the other leftover
term. `redhat-annobin-cc1` loads a gcc plugin that stamps build provenance
into a `.gnu.build.attributes` section; the cross toolchain has no such
plugin, so packages built here carry no annotations. Nothing in this project
reads them, and `annocheck` is not part of any gate — but that is an argument
from what we do rather than a measurement of what a vendor spec might expect.
