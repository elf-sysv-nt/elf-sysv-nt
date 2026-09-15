# DR-0110 — the libgcc selection is measured from the driver, because the dump does not carry it

Status: provisional
Date: 2026-09-15
Deciding: the build worker, on the first run of `install-specs` against the real
cross toolchain; the operator ratifies
Proposal: none; taken when the generator DR-0108 specified refused its own
output and restored the file it was replacing
Amends: doc/design/target-definition.md § The PIE default
Partially supersedes: DR-0108, on where the generated file's `*libgcc` comes
from; the rest of DR-0108, including the reason a hand-written fragment cannot
be installed, stands

## What was decided

The generated specs file takes its `*libgcc` stanza from what the driver
actually passes, arm by arm, and not from `-dumpspecs`. Each arm of the
selection is asked of the driver that can reach it, the text it produces
becomes that arm, and the whole file is then held to linking exactly as the
driver links with no file at all — across every kind of link the selection
distinguishes and through both the C and the C++ driver.

The bar is equality in both directions. A file that loses nothing can still be
wrong, and two of the three shapes tried before this was measured were wrong in
exactly that way: they added a shared libgcc to every C program and lost
nothing doing it.

## Why the dump is not enough

DR-0108 rested on one sentence: that `-dumpspecs` prints the post-`init_spec`
table. On this toolchain it does not. With no specs file installed, gcc 13.3.0
for this target prints

    *libgcc:
    -lgcc

while the same driver, on the same run, hands the linker `-lgcc_s -lgcc` for a
C++ program and `-lgcc -lgcc_eh` for a `-static-libgcc` one. The selection is
real and the dump does not carry it, so a file built from the dump alone moves
every link onto the static libgcc — the second half of what spike 53 measured,
reintroduced by the repair written for it. The generator caught this itself, on
its first run against the real compiler, and put the old file back.

Reconstructing the stanza from gcc's own source did not work either. `gcc.cc`'s
`init_gcc_specs` writes one of three shapes depending on configuration, and all
three were tried and all three failed: this driver passes no `-lgcc_eh` to a C
link at all, which none of the three produces. Working out which configure
switch explains that is archaeology; asking the driver is a measurement.

The measurement also turned up the thing that makes this record necessary
rather than a detail. The two drivers do not agree. `gcc -shared-libgcc` selects
the static arm, and `g++` with no switches at all selects the shared one, so a
single stanza cannot reproduce both exactly. One difference survives: a C link
with `-static-libgcc` gains `-lgcc_eh`. That is an archive of the unwinder's own
routines, and a C program that raises nothing references no member of it, so
the gain is a name on the command line and nothing in the image. It is allowed
by name in the generator, with that reasoning beside it, and every other
difference is a failure.

## Consequences

`toolchain/gcc/install-specs` derives the stanza and verifies over seven probes
rather than one. The seven are the reachable arms — plain, shared-libgcc,
static-libgcc, static, and shared — through whichever driver reaches each.

The third state DR-0061 and DR-0108 were reaching for now exists and has been
measured on the real toolchain rather than a stand-in:
`toolchain/gcc/t/accept2.sh` reports 17 claims of 17, and
`toolchain/gcc/t/granule-default.sh` reports 6 of 6, with one specs file
installed. The granule default and the C++ unwinder hold together, which is
what the working note behind DR-0108 said the tree could not do.

DR-0108's own verification was written against a stand-in — a Linux gcc 11
reached through a `-B` prefix — and that stand-in did not reproduce this. Its
"Not verified" section said the first real run would be the next `build-gcc2`;
this is that run, and it found the defect the stand-in hid. A stand-in that
agrees with the real thing about the question asked can still disagree about
the question not asked.

## What it does not decide

Which layer carries the target-mandated link default. DR-0108 parked that at
tier 8 and it stays there, with `spike/link-default-layer/` under it. This
record makes the specs-file mechanism work; it does not argue that the specs
file is where the default belongs.

Why this driver's C and C++ paths disagree about `-shared-libgcc`. The
behaviour is measured and reproducible and the cause is not identified.

## Not verified

That the seven probes are every arm that matters. They are the arms the
selection's own conditionals distinguish, reached through both drivers; a link
shape that selects differently for a reason not in that text would not have
been asked.

That the allowed `-lgcc_eh` gain is harmless in every case rather than in the
ordinary one. The argument is that the linker pulls no member from an archive
nothing references, which is how static archives behave; no C program was built
both ways and compared.

That another gcc release behaves this way. Everything here is gcc 13.3.0 for
one target, which is what `toolchain/gcc/gcc.pin` names. The generator does not
assume otherwise: it measures on each run, and refuses rather than guesses.
