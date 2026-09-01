# DR-XXXX — CET opt-out belongs in the toolchain default, not only the rpm macros

Status: accepted
Date: 2026-09-01
Deciding: the build worker, on a requirements audit, a defensible call the operator
may revisit
Proposal: none; taken when an audit found CET opt-out enforced at the rpm-macro and
loader layers but absent from the compiler default the way max-page-size is present.

## What was decided

Every object the target compiler emits must be built `-fcf-protection=none`, and no
image may expect CET (shadow stacks or indirect-branch tracking), because the
process runs with them opted out — the loader stub refuses to start when user
shadow stacks are enabled. This records that opting out is a requirement on every
compile the toolchain performs, and that its home is the compiler default, beside
the max-page-size default already there, not only the rpm macro set.

## Why the current placement is a half-recorded obligation

The opt-out is real and load-bearing: `toolchain/rpm/macros.elfsysvnt` puts
`-fcf-protection=none` in `%build_cflags`, and `loader/exec/stub.c` enforces the
process-level half by refusing a load under enabled shadow stacks. But the compiler
default carries only max-page-size, not this — so an object compiled by the target
gcc outside an rpm build does not get the opt-out, and every `veneer/wiring/t/live-*.sh`
passes `-fcf-protection=none` by hand. A flag that every script repeats is the same
tell the granule requirement gave: the obligation is known and applied ad hoc, not
defaulted where the toolchain would guarantee it. The asymmetry is the evidence —
max-page-size is trusted to the default; CET, no less required, is not.

## Consequences

The opt-out moves to the compiler default (`toolchain/gcc/default.specs`, beside
max-page-size), so every compile by the target gcc is CET-free whether or not it
runs under the rpm macros, and the per-script flags become redundant rather than
load-bearing. The rpm macro stays as the package-build layer and the loader stub
stays as the process-level backstop; this adds the missing default beneath them.

To verify: `toolchain/rpm/README.md` lists under "Not verified" that CET can be
disabled this way and that it has not been tested together with the rest, so
moving it to the default is also the occasion to confirm the target gcc honors
`-fcf-protection=none` and that a CET-free object loads and runs clean.

## What it does not decide

Whether the platform will ever support CET rather than opt out, which would be a
runtime and loader change, not a default. And the enablement mechanism on the
Windows side (CFG, shadow stacks), which the loader stub reads and refuses; this
record is about the compiler's output, not the host's setting.
