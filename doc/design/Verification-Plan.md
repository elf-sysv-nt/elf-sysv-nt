# Verification plan

What counts as proof, for each conformance class `doc/design/Requirements.md`
states.

Almost none of this is new. The differential oracle, the substitution rule,
the fuzz obligations and the spike contract are all written down somewhere in
the tree already, in a delivery note or a convention or a record. Writing them
once here changes one thing: a certification can be read against a bar that
existed before it ran, instead of against the bar its own delivery note set.
That was the recurring failure the design-gaps review found, and it is what
this document is for.

## The oracle a differential runs against

A differential test compares this platform against a real el8 kernel, and
the kernel it compares against is Rocky Linux 8.10's, reached through the
`rocky8` WSL instance this host carries. Not whatever kernel WSL happens to
run for that instance, which is Microsoft's and not el8's 4.18: S4 in
`doc/design/substitutions.md` records that substitution and what closes it.

That distinction is why this section exists at all. A comparison environment
that every delivery note apologizes for and no document pins is not an
environment; it is a habit, and a habit survives being noticed. Three
certifications resting on the same acknowledged substitute is the shape the
habit takes, and the third acknowledgment carries the finding rather than the
first.

So: a class B claim is proved against the oracle, or it is proved against a
substitute and carries a row in `doc/design/substitutions.md`. There is no
third option, and a run that hides which of the two it did is the failure the
whole rule exists to prevent.

The oracle runs the same program the platform runs. `test/core/lksys.h`
builds a freestanding program once against the gate and once against the
`syscall` instruction, so what is compared is the kernel's behaviour under
the same sequence of calls, never a stand-in that could not fail
(`Core-Phase1.md` D2).

## The bar per conformance class

Class A, bit-exact, is proved by comparison against el8's own kernel headers
rather than by test. A syscall number, a constant, a struct layout and the
auxiliary vector's shape are all things the headers state outright; the
check reads them and asserts equality, and a difference is a defect without
any argument about whether it matters. The tables under `core/` are written
from those headers and say so at the top; a table large enough to be typed
wrong is generated from them.

Class B, behaviourally equivalent, is proved by differential against the
oracle above. The pattern is fixed: construct a sequence, run it on el8's
kernel, run it here, compare the trace of results and errnos. Criterion 2's
291-line file trace is the worked example, and every criterion below that
names a differential is the same shape. A specification reading is not a
proof of class B, because the manual is what both implementations claim to
follow and the disagreements live in what it leaves open.

Class C, divergence with a recorded delta, is proved by the record. The
obligation is not a test but a document: what differs, where a caller can
observe it, and why the alternative was refused. `Core-Phase2.md` § Recorded
divergences is the register for the file system; a later phase's document
carries its own. A class C claim with no record is a class A or B failure
wearing a different name.

Everything in every class also has to survive the gate below. Passing a
differential and breaking a suite is not a pass.

Settled by: DR-0105.

## The gate

`ci/gate.sh`, run by a `pre-merge-commit` hook, over every suite registered in
`ci/suites.txt`. A non-zero exit from any suite aborts the merge before its
commit exists. There is no service, no runner farm and no badge, and the cost
of that is stated rather than hidden: the gate guards merges made where the
hook is installed, `--no-verify` walks past it, and an empty registry fails
outright, because a gate that runs nothing and passes is worse than none.

The gate runs on the build host DR-0038 names, which 0011 keeps as the
build environment and nothing more: the substrate, the core and the
harnesses are built there with the mingw cross compiler and the target
toolchain, and no host library stands in the path of a Linux program.
DR-0035 is the record for the gate and DR-0038 for the host it runs on;
DR-0038 reaches that one point of DR-0035 and no other, so both records
stand and the current reading is the pair.

### What the registry must carry

`test/suites.tsv` is the registry of every suite in the tree, held closed
both ways by `bin/check-suites`, and its gate tier is held one-to-one with
`ci/suites.txt`. The gate tier carries the document checks and the
substrate-line check: fast, offline, deterministic, needing nothing outside
the checkout. The report tier carries what needs a toolchain, the oracle or
WSL: the substrate conformance suite, the host file store's bar, and the
kernel's criteria as they are met. A suite that would skip when its inputs
are absent does not join the gate, because a gate that passes because it
could not run reports green for the one condition under which it knows
nothing; it joins when a missing input makes it fail rather than skip.

Settled by: DR-0035, DR-0038.

## Substitution

A certification run against a substitute for the thing it certifies is
permitted, and it creates a row in `doc/design/substitutions.md`: what was
substituted, for what, where, and what burns it down. The row closes when the
certification reruns against the real target and matches, or when the
divergence it finds is written down as justified.

An open row is a bounded claim, and the bound belongs in the claim. S2
substitutes the flags Red Hat's hardened specs files inject, spelled out, for
running the build under those specs files through rpm; the claim it supports
is therefore about image shape and nothing about the hardening the other
optflags provide, and it is a substitution against the class A bar for
substrate N's rebuilt images. S3 substitutes the absence of annobin
annotations for the annobin plugin itself, which is a substitution against
the same bar, narrowed to build provenance that nothing in this project
reads. S4 substitutes WSL's kernel for el8's 4.18 beneath the oracle's
userland, which bounds every class B claim until a 4.18 kernel is reachable.

A certification that hides its substitution rather than recording it is not a
weaker certification. It is not one.

## Acceptance

The acceptance harness reports the two counts `doc/design/Requirements.md`
names, one per substrate, and it credits a package when the package's own
test suite passes under that substrate with the package unmodified and no
substitution open against it. Nothing narrower counts: a package that
links, or starts, or prints its version, is a package that has not been
accepted.

Under substrate N the harness rebuilds from Rocky 8.10's source RPMs with
the target toolchain and the rpm macros under `toolchain/`, and the
post-link check that refuses a raw `syscall` or a `%fs` load is part of
the rebuild, so a package the check refuses is counted as not rebuilt
rather than as failed. Under substrate H the harness installs Rocky 8.10's
binary RPMs and runs the same tests. `acceptance/` carries the package
framing both inherit; its harness is rewritten when a package can first be
run end to end, which is phase 9.

Settled by: DR-0104.

## The kernel's criteria

Proposal 0011's seventeen verification criteria are the criteria of record
for the kernel at the syscall boundary: each a command that exits zero or a
state a script can check, registered in `test/suites.tsv` at the tier the
criterion names. Two are amended. Criterion 3 reads the LX-metadata tree
back under WSL only; Cygwin 3.6.10 does not read the metadata (spike 39), and
what it sees is recorded in the VFS design rather than tested. Criterion 4
is per substrate: a median under 2 ms for the fork under H, and under N the
fork completing with its median recorded beside Cygwin's and a regression
band of 1.5 times the value recorded at certification on the same host.
Criterion 1 is met (`core/run.sh`, `Core-Phase1.md`, its remainder with
Phase 2), criterion 2 is met (`test/t/vfs-diff.sh`) and criterion 3 as
amended is met (`test/t/lxfs-interop.sh`), both recorded in
`Core-Phase2.md`; criteria 4 to 17 are unbuilt.

Criterion 1's harness is not the file 0011 names. The proposal wrote
`test/t/hello-static.sh` at the gate tier before phase 1 had a directory;
what was built is `core/run.sh`, at the report tier, because a suite needing
both toolchains and a live Rocky 8 cannot gate a merge. The criterion is
unchanged and the addendum of 2026-09-06 records the rename. Where a
criterion's harness moves, this section names the file that meets it and the
proposal keeps the criterion, so the two never have to be reconciled by a
reader guessing which is current.

Settled by: DR-0102.

## Fuzz and unit obligations

Anything that parses attacker-shaped input from its first line gets unit tests
over recorded fixtures and a fuzz target fed malformed and truncated input.
That is the kernel's ELF mapper, the path walk, and every syscall that
copies a structure in from user memory, and the obligation is discharged
alongside the implementation rather than after it. A mapper that has never
seen a truncated `PT_LOAD` is not finished, and a fuzz crash fails the gate
the same way a failing assertion does.

Build leaf to trunk. Nothing depends on functionality whose tests have not been
written and have not passed.

## The spike contract

A spike is a characterization experiment kept so that a finding can be
re-measured rather than believed. It lives in `spike/<question>/` with the
script that measured it, its sources, and the transcript the script produced,
named by the date of the run. Rerunning the script regenerates the transcript
on the same machine; a spike whose script no longer runs is a failing test and
not a stale note.

`test/spike-regen.tsv` is the register, and a spike that has reached its
verdict with its transcript is a point at which work is worth publishing.

## Not verified

That the class A tables were written from el8's kernel headers everywhere
they claim to. `core/lxtypes.h` says so at its head; whether every value in
it was read from a header rather than typed from a manual has not been
swept, and the generation the class A bar names has not been built.

That the registry covers what the classes above require. It grows one line
per suite, which means its coverage is whatever the suites have contributed
and not a set anybody has checked against this document.

That the oracle is reachable on demand. The harnesses resolve to a Rocky
8.10 WSL instance named `rocky8`, and nothing pins that instance's
existence; a machine without it can run no class B test at all.

That the fuzz obligation has been met for anything yet. The mapper and the
walk have unit bars and differentials; neither has a fuzz target.
