# Proposal 0013 — the governed set after 0011: Requirements, the verification plan, the target definition, and the roadmap

Status: accepted
Author: drafted 2026-09-06 for Philip Dye, under the grant of 2026-09-05
Date: 2026-09-06
Analysed against: `47726d8` on `main`; the audit note
`doc/history/reviews/veneer-text-audit-2026-09-06.md` for every finding
Classification: expensive to undo in one respect only. Three of the four
documents state promises (what the platform is for, what counts as
accepted, which loader runs), and a promise a reader has relied on is not
withdrawn by editing the file. The text itself is a two-way door. Proposal
rather than fold-back because five of the findings have no record behind
them since DR-0097 retired the veneer's, and a rewrite that stated them
would be settling them by editorial act.

## Context and scope

The incident note of 2026-09-05 found `Architecture.md` describing substrate
N as though it were the whole system, because it had been compressed from
records taken before H existed. That document was rewritten the same day.
It named four others as unread: `doc/ROADMAP.md`,
`doc/design/Requirements.md`, `doc/design/Verification-Plan.md` and
`doc/design/target-definition.md`. The audit read them on 2026-09-06 and
marked every mechanism sentence N, H, both or veneer. Its tables are the
evidence for this proposal and are not repeated; what follows is what to do
about them.

In scope: those four documents, one retirement into `doc/history/`, the
records the rewrite owes, and one disagreement between `Architecture.md`
and 0011 that the audit surfaced and that cannot be left standing in a
governed set. Out of scope: `Core-Phase1.md`, `substitutions.md`,
`test-environment.md` and `toolchain-bootstrap-stages.md`, which are N's
build notes and were not named by the incident; the code.

## Goals and non-goals

Goal: after this lands, a reader who opens any governed document meets the
kernel at the syscall boundary with its two substrates, and every sentence
about a mechanism says which substrate it is true of. Goal: the promises
those documents make (scope, non-goals, the acceptance bar, the
conformance classes) are each backed by a record, so `check-design-links`
can hold them. Goal: the documents stay the length they are; a rewrite
that doubles a document has replaced it with a proposal.

Non-goal: setting the acceptance number. It is the operator's blank and
stays one. Non-goal: designing N's loader. The proposal settles which
loader the design names and leaves its construction to phase 3.

## The design

### 1. `doc/ROADMAP.md` is retired and replaced

The file moves whole to `doc/history/veneer-roadmap.md` with a two-line
header saying what it was and when it stopped being true, as
`veneer-address-space.md` was moved by DR-0100. A new `doc/ROADMAP.md` of
about forty lines says: the order of work is 0011 § 18 with 0012's phase 0
in front of it and the seam's realisations after phase 2; the current
position is read from `test/suites.tsv` and `doc/milestones.md`, never from
this file; and the three decisions the old table tracked are settled
(DR-0001 for the triple, DR-0101 for the thread pointer under N, 0011 § 1
for the boundary itself). Nothing in it is a mechanism.

### 2. `Requirements.md` is rewritten to the same outline

Scope, from 0011's goals: a Linux kernel personality for Windows NT, one
core over two substrates, presenting el8's kernel ABI upward so that el8's
userland runs, rebuilt against the gate under N and as shipped under H.
The three obligations become: an el8 source package rebuilds against N
with no change to the package (unchanged in force); a package runs, under
either substrate, with the kernel mapping its image and glibc's own `ld.so`
loading the rest; a running package observes el8's kernel semantics, which
are Linux's by construction (0011 § 1).

Non-goals, from 0011: no security boundary, no init system, no network
namespaces or the container stack, and, under N only, no shipped binary
that carries a raw `syscall` or a `%fs` load (the post-link check). The
four veneer non-goals go, with "not a Linux kernel" inverted explicitly so
a reader of the old text is not misled by memory.

Conformance classes, redefined at the boundary. Class A, bit-exact: the
syscall numbers, the constants, the struct layouts crossing the boundary,
`uname`, the auxv's shape; checked against el8's kernel headers and
generated from them where a table is large enough to be typed wrong. Class
B, behaviourally equivalent: everything a criterion measures against the
Rocky 8 oracle. Class C, a recorded divergence: what the VFS design records
where NTFS refuses Linux's semantics (spike 39 q9 has the first two). The
sentence "every symbol on the face belongs to exactly one class" becomes
"every syscall the table implements belongs to exactly one class, and the
table under `core/` is where that assignment lives".

Acceptance, per substrate. Under N: the operator's count of packages that
rebuild from source, run, and pass their own test suites unmodified, as
now. Under H: the count of el8 packages that install from Rocky's RPMs and
pass their own test suites unmodified, which is the same sentence with the
rebuild removed. The demand census paragraphs are replaced by two: what
the old census measured and why its bound no longer applies, and what spike
51's census bounds instead (the N rebuild's reach). DR-0079's counters go.

### 3. `Verification-Plan.md` is rewritten in four sections and amended in two

Replaced: the bar per class (class A against kernel headers; class B the
criteria's differential; class C the VFS design's record), the registry
(`test/suites.tsv` at its tiers, with the substrate conformance suite and
the twelve criteria; the empty-registry rule kept), acceptance (restated
per substrate as § 2 above), and not-verified. Amended: the floor (the
oracle is el8's kernel behaviour through the `rocky8` instance, with S4's
substitution row already carrying the WSL kernel standing in for 4.18;
the rule that a claim is proved against the oracle or carries a row is
unchanged), and the gate (DR-0035's mechanism kept; the "floor and
derivation" reference and the 3.0.7 sentence removed; DR-0038's root named
as the build host, which is what 0011 said it remains). Kept: substitution,
the kernel's criteria, fuzz and unit obligations (with the ELF mapper named
beside N's loader), the spike contract.

### 4. `target-definition.md` is amended in place

The six values and the table do not change. Rewritten: the limit of the
`linux` claim, which becomes the inversion Architecture § Target and claim
already states, in this document's voice, with DR-0005 cited as the record
the inversion reopened and the ratifying record cited as what inverted it;
the prose under `uname`, which now says there is a kernel with 4.18's ABI
and an unimplemented call returns `ENOSYS`; the loader SONAME's third
paragraph, per § 5 below. Stripped: every `WP-` citation (the work packages
are retired; the sentences stand without them) and the two DR-0028
citations (retired by DR-0100; the non-PIE image is the kernel's mapper's
job under both substrates). Settled: `.note.elfsysvnt.abi`, per § 6.

### 5. Which loader runs under N

Architecture § The loader says the platform's own loader, with its own
cache format (DR-0011) and its own relocation certification (DR-0016).
0011 § 5 says the kernel is not a dynamic loader and has none; `ld.so` is
glibc's, unmodified, and § 16 rebuilds glibc, `ld.so` included, against the
gate. These are two designs. This proposal recommends 0011's: the kernel
maps the program and its `PT_INTERP`, and glibc's `ld.so`, rebuilt for N
and shipped for H, does the rest. The case is that a loader is a large
program the project would otherwise write twice (once as `ld.so` for H,
once as its own for N), that the differential the criteria run is against a
userland whose loader is glibc's, and that DR-0011, DR-0016, DR-0022 and
DR-0073 describe a loader that existed for the veneer and were kept by
DR-0097 on the reading that "the new design still has [it], in different
clothes", which 0011 § 5 says it does not. The Architecture paragraph is
rewritten; those four records retire under this proposal's ratifying
record (DR-0027's exec classifier and interpreter limit stand; they are the
kernel's).

The alternative, keeping the platform's loader for N, is considered below.

### 6. `.note.elfsysvnt.abi`

The note carried the DLL's compatibility counter (DR-0018, retired). Under
H nothing emits it and nothing reads it. Recommendation: the carrier stays
as a reservation (the section name, owner and type are fixed and cheap) and
the payload becomes the gate ABI version N's rebuilt glibc was built
against, one 32-bit word, so that a kernel can refuse a binary built for a
gate it no longer offers. Under H the note is absent and its absence means
"shipped el8, no gate". `bin/check-target-definition` is unchanged.

## Alternatives considered

Rewrite `ROADMAP.md` rather than retire it. Every section's subject is the
DLL bootstrap; a rewrite would be 0011 § 18 restated, which is the copy
Requirements.md's preamble warns against.

Delete `Requirements.md` and let 0011 be the requirements. 0011 is 1,400
lines and a proposal; a reader owed the promise in one page is owed it in
one page. Kept, rewritten.

Keep the platform's loader for N (against § 5). It exists in part (the
graph, the cache, the relocation certification under `loader/` in the
veneer tree, none of it in this repository), and owning the loader gives
the kernel a place to put N-specific behaviour. Against: the behaviour is
glibc's to have, 0011 § 5 is the ratified design, and the kernel under H
has to run glibc's `ld.so` anyway, so N's would be the second loader in the
system. Not taken; the operator may take it, and the cost is that phase 3
writes a loader.

Drop `.note.elfsysvnt.abi` entirely (against § 6). Cheaper, and nothing
today reads it. Against: the gate ABI will change at least once before the
version floor run, and a rebuilt userland from before the change will
otherwise fail at the first syscall with no diagnostic. Not taken.

Fold the four documents back without a proposal. The audit found five
findings with no record behind them; DR-0075 says the governing documents
cite their records, and there would be nothing to cite.

## Cross-cutting concerns

Records owed on acceptance: one ratifying record for this proposal, which
also retires DR-0011, DR-0016, DR-0022 and DR-0073 if § 5 is taken as
recommended, and rehomes DR-0027 to Architecture § The loader; one record
for the acceptance bar per substrate (§ 2), homed in Requirements.md; one
for the conformance classes at the boundary (§ 2), homed in Requirements
and Verification-Plan; the `.note.elfsysvnt.abi` payload (§ 6) is a
paragraph of the ratifying record, not its own. Requirements.md and
target-definition.md gain Settled-by lines, which `check-design-links`
will then hold.

`check-design-links`' GOVERNED tuple is unchanged; `ROADMAP.md` was never
in it. `check-doc-refs` will follow the moved file. `check-target-definition`
is unaffected.

Compatibility with what exists: `core/`, `substrate/`, `seam/`, the spikes
and `test/suites.tsv` are untouched. Nothing under N's phase 1 depends on
which loader phase 3 builds.

## Verification criteria

1. Every sentence in the four documents that states a mechanism names N,
   H or both, or is true of both without naming either; a reviewer reads
   each document once against the audit's table and finds no row left
   marked veneer. This is the criterion Architecture.md was rewritten to.
2. `bin/check-design-links`, `check-doc-refs`, `check-worknote-refs` and
   `check-target-definition` pass on the merged tree.
3. `Requirements.md`, `Verification-Plan.md` and `target-definition.md`
   each carry a Settled-by line naming the records of the paragraph above.
4. `Architecture.md` § The loader and 0011 § 5 agree.
5. Each document is within twenty percent of its present length.

## Open questions

1. § 5, the loader under N. Recommendation: 0011's. Tier 8; the operator's.
2. § 6, the note's payload. Recommendation: the gate ABI version. Tier 8.
3. The acceptance bar under H: "installs from Rocky's RPMs and passes its
   own test suite" is the recommendation; whether the count under H is
   reported beside N's or replaces it in the headline is the operator's.
4. Whether `doc/history/veneer-roadmap.md` keeps the old file's "Not
   verified" section. Recommendation: yes, whole; history is not edited.

## Not verified

That 0011 § 16's rebuilt glibc actually boots its own `ld.so` against the
gate without changes beyond the `sysdeps` files § 16 lists. Phase 3
measures it; this proposal assumes it, as 0011 did.

That the class A tables can be generated from el8's kernel headers rather
than checked against them. The kernel's syscall table is 335 entries and
the struct set is a few dozen; generation is the cheaper path if the
headers parse cleanly, and nobody has tried.

## Decision log

Each entry names the tier of the decision ladder that settled it.

D1. Audit before rewrite. Tier 1: the audit found two documents disagree
on the loader, which a rewrite without it would have reconciled silently
in whichever direction the writer remembered.

D2. Retire the roadmap rather than rewrite it. Tier 1: no sentence
survives; tier 2 (economy) against restating 0011 § 18.

D3. Keep the three governed documents at their outlines and lengths. Tier
5 (diagnosability): a reader who knew the old document finds the same
headings and reads the difference.

D4. Redefine the classes at the boundary rather than drop them. Tier 2:
the criteria already sort into A (tables), B (oracle) and C (recorded
divergence), and the plan's proof rule per class is what makes a
certification readable against a bar that existed before it ran.

D5. The loader under N. Tier 1 discriminated between the documents (they
contradict); the choice between the designs is tier 8, reserved, with the
recommendation stated in § 5 and the alternative kept.

D6. The note's payload. Tier 8, reserved; recommendation in § 6.

D7. Stop here. The grant of 2026-09-05 covers the audit and the proposal;
acceptance belongs to the operator, and no document is edited until it is
given.

D8 (2026-09-06, on acceptance). The operator accepted the proposal as
written with the grant "proceed without stopping". D5 is revised: the
loader question is not a choice between two designs but a governing
document contradicting the ratified design (0011 § 5, DR-0097), which the
ladder's first tier settles for 0011's; DR-0103 says so and retires
DR-0011, DR-0016, DR-0022 and DR-0073. D6 is revised: tier 2 (reliability
over time) picks the gate-version payload over dropping the note, since a
changed gate would otherwise fail a stale image at its first call with no
diagnostic. Open question 3 is the tier-7 default, two counts side by side,
with the numbers still the operator's blanks (DR-0104). Open question 4:
the retired roadmap keeps its "Not verified" section whole.
