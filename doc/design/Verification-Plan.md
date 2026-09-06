# Verification plan

What counts as proof, for each conformance class `doc/design/Requirements.md`
states.

Almost none of this is new. The differential floor, the substitution rule, the
fuzz obligations and the spike contract are all written down somewhere in the
tree already, in a delivery note or a convention or a record. Writing them once
here changes one thing: a certification can be read against a bar that existed
before it ran, instead of against the bar its own delivery note set. That was
the recurring failure the design-gaps review found, and it is what this
document is for.

## The floor a differential runs against

A differential test compares this platform against a real glibc, and the glibc
it compares against is el8's 2.28 on the RHEL root. Not Ubuntu's 2.43 through
WSL, and not whatever the shell that ran the test happened to have.

That distinction is why this section exists at all. A comparison environment
that every delivery note apologizes for and no document pins is not an
environment; it is a habit, and a habit survives being noticed. Three
certifications resting on the same acknowledged substitute is the shape the
habit takes, and the third acknowledgment carries the finding rather than the
first.

So: a class B claim is proved against 2.28, or it is proved against a
substitute and carries a row in `doc/design/substitutions.md`. There is no
third option, and a run that hides which of the two it did is the failure the
whole rule exists to prevent.

The floor is reachable through the `LINUX_REF_DISTRO` seam, which selects the
reference userland a differential runs against. S1's closed row in
`doc/design/substitutions.md` is the worked example of a substitution burning
down through that seam.

## The bar per conformance class

Class A, bit-exact, is proved by comparison against el8's own headers and
binaries rather than by test. A struct layout, a constant value, a symbol
version node and an image's shape are all things the vendor's artifacts state
outright; the check reads them and asserts equality, and a difference is a
defect without any argument about whether it matters. `veneer/include/` is
vendored byte-identical for exactly this reason, so that the arithmetic a
header performs is el8's arithmetic and not a paraphrase of it.

Class B, behaviourally equivalent, is proved by differential against the floor
above. The pattern is fixed: construct a case, run it on el8's glibc, run it
here, compare the observable result. Resolution order over a graph with
collisions, initializer order with a cycle in it, the auxiliary vector's
contents, the loader's search path. A specification reading is not a proof of
class B, because the specification is what both implementations claim to
follow and the disagreements live in what it leaves open.

Class C, divergence with a recorded delta, is proved by the record. The
obligation is not a test but a document: what differs, where a caller can
observe it, and why the alternative was refused. A class C claim with no
record is a class A or B failure wearing a different name.

Everything in every class also has to survive the gate below. Passing a
differential and breaking a suite is not a pass.

## The gate

`ci/gate.sh`, run by a `pre-merge-commit` hook, over every suite registered in
`ci/suites.txt`. A non-zero exit from any suite aborts the merge before its
commit exists. There is no service, no runner farm and no badge, and the cost
of that is stated rather than hidden: the gate guards merges made where the
hook is installed, `--no-verify` walks past it, and an empty registry fails
outright, because a gate that runs nothing and passes is worse than none.

The gate certifies on the primary Cygwin root, the 3.6.10 installation that
`doc/design/Architecture.md` § Floor and derivation describes; where that root
sits on a given machine is a working note rather than a governed fact. The
3.0.7 verification root holds no build or certification role: its gcc 7.4 can
neither build the 3.6.10 runtime nor compile the modern certifications. DR-0035
is the record for the gate and DR-0038 for the root it certifies against;
DR-0038 reaches that one point of DR-0035 and no other, so both records stand
and the current reading is the pair.

### What the registry must carry

The registry is four entries: the ELF parser suite and three documentation
checks. Every other certification in the tree — the exec bar, the realproc
layer, the face crossings — is run by hand, which means the non-regression
guarantee that several records rest on is enforced by nothing.

The exec suite joins the registry, decided on 2026-09-03. It is the bar three
records already treat as the guarantee, and registering it is what makes that
treatment true.

The face certifications do not join it yet, and the reason is the same one that
makes an empty registry fail outright. They skip when the built runtime or the
build tree is absent, and a gate that passes because it could not run is worse
than no gate: it reports green for the one condition under which it knows
nothing. They join when a missing build product makes them fail rather than
skip.

The bar itself needs stating once. It is currently enumerated four different
ways across four documents, and the suite runs a step that is absent from its
own plan, so a reader cannot tell what passing means. Whichever enumeration is
right, one of them is, and the others are corrected to match it.

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
optflags provide, and it is a substitution against the class A bar. S3
substitutes the absence of annobin annotations for the annobin plugin itself,
which is a substitution against the same bar, narrowed to build provenance
that nothing in this project reads.

A certification that hides its substitution rather than recording it is not a
weaker certification. It is not one.

## Acceptance

The acceptance harness reports the count `doc/design/Requirements.md` names,
and it credits a symbol when something certified stands behind it. A forward
counts. A filled stub counts, because DR-0052's synthesized body is certified
and its delta is nil or recorded. A shim counts once its slice has been
live-crossed, and not before: a translation no crossing has checked is a
promise, and the harness does not credit promises.

What `ready` withholds is worth stating, because the harness's own word for
success is narrower than the requirement's. A package reads `ready` when every
symbol it imports has a certified body behind it. That says the veneer is
complete for that package. It does not say the package ran, and running it
needs the loader's dynamic-exec path to stand in for `ld-linux` and resolve
`libc.so.6`, which is the loader's surface. Acceptance in
`doc/design/Requirements.md` is the stronger claim, and the harness's `ready`
is one step short of it.

A shim counts once a body exists for it. The harness reads
`veneer/wiring/bodies.tsv`, curated one symbol at a time, and credits a
bucket-3 shim only where that manifest names it and its slice has been
live-crossed. Crediting the slice instead is what DR-0057 did and DR-0085
withdrew: a slice was certified by the existence of its crossing script, so
sixty-four shims were credited on five bodies, and the first pinned package
read `ready` on translations nobody had written.

`ready` also changed shape when the claimed surface did. It used to mean every
import has a body; it now means every import is claimed and has a body, which
is the same statement with its second half made explicit. A package importing
a name the veneer does not claim never reaches the harness at all, because it
fails at link.

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

The sections above and below this one describe the veneer arc's classes and
harness, which the audit proposal 0012 names as owed has not yet read against
0011; until it does, where they and 0011's criteria disagree, the criteria
are current.

Settled by: DR-0102.

## Fuzz and unit obligations

Anything that parses attacker-shaped input from its first line gets unit tests
over recorded fixtures and a fuzz target fed malformed and truncated ELF. That
is the loader, the relocator, and the verdef and verneed matcher, and the
obligation is discharged alongside the implementation rather than after it. A
relocator that has never seen a truncated `PT_DYNAMIC` is not finished, and a
fuzz crash fails the gate the same way a failing assertion does.

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

That class A comparisons actually read el8's artifacts everywhere they claim
to. The vendored headers do; whether every constant table in the wiring layer
was derived from them rather than typed from a manual has not been swept.

That the gate's suite registry covers what the classes above require. The
registry grows one line per package, which means its coverage is whatever the
packages have contributed and not a set anybody has checked against this
document.

That the differential floor is reachable on demand. The seam resolves to a
Rocky 8.10 WSL instance named `rocky8`, and nothing pins that instance's
existence; a machine without it can run class B tests only against a
substitute.

That the three classes are provable at all for the fault path. The ABI
boundary's upward direction is specified and not built, per DR-0042, and no
bar in this document is currently satisfiable for it.
