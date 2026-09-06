# DR-0103 — proposal 0013 is ratified: the governed set states the kernel, glibc's `ld.so` is the loader, and the loader records retire

Status: accepted
Date: 2026-09-06
Deciding: the run, under the operator's grant of 2026-09-06 ("proceed without stopping"), on proposal 0013 as written
Proposal: 0013
Supersedes: DR-0011, DR-0016, DR-0022, DR-0073
Amends: doc/design/Architecture.md § The loader

## What was decided

Proposal 0013 is ratified. The four governing documents the incident note of
2026-09-05 left unread are rewritten to the design of record: `ROADMAP.md`
retires whole to `doc/history/veneer-roadmap.md` and a page replaces it;
`Requirements.md` is rewritten to its outline with scope and non-goals from
0011's goals, the conformance classes redefined at the syscall boundary and
acceptance per substrate; `Verification-Plan.md` is rewritten in four
sections and amended in two; `target-definition.md` is amended in place,
its six values unchanged.

The loader under substrate N is glibc's own `ld.so`, rebuilt against the
gate, as 0011 § 5 and § 16 state and as the kernel under H already
requires: the kernel maps a program and the interpreter its `PT_INTERP`
names, builds the initial stack, and jumps to the interpreter's entry. It
has no dynamic loader, no loader cache, no relocation of its own, and no
link map to maintain. DR-0011 (a loader cache in this project's format),
DR-0016 (relocation types certified against vendor objects), DR-0022 (the
rendezvous link map) and DR-0073 (a weak undefined symbol resolves to zero)
describe a loader that existed for the veneer; they were kept by DR-0097
on the reading that the new design "still has [it], in different clothes",
which 0011 § 5 says it does not. They retire here. DR-0027, the exec
classifier and the four-hop interpreter limit, is the kernel's and stands,
homed in `Architecture.md` § The loader.

`.note.elfsysvnt.abi` keeps its carrier (owner `ELFSYSVNT`, type 1) and
changes its payload: one 32-bit word, the version of the gate ABI the image
was built against, emitted by substrate N's rebuilt glibc and read by the
kernel at `exec`, which refuses an image built for a gate it no longer
offers with a diagnostic. Under substrate H the note is absent and its
absence means "shipped el8, no gate".

## Why

The audit of 2026-09-06 (`doc/history/reviews/veneer-text-audit-2026-09-06.md`)
found the four documents stating the veneer's promises in the present
tense and found `Architecture.md` § The loader contradicting 0011 § 5. A
governing document states what the ratified records settle; 0011 is
ratified (DR-0097) and describes the loader as glibc's, so the
contradiction is a documentation error against the design of record, not
a choice between two designs. Proposal 0013 listed the loader as a tier-8
question out of caution; the ladder's first tier settles it, and D5 of the
proposal's log is revised to say so.

The note's payload was decided at tier 2. Both candidates (drop the note;
carry the gate version) are correct today, when there is one gate ABI. A
gate that changes, which the version-floor run and the `%gs` carrier's
history make likely, leaves a rebuilt userland from before the change
failing at its first system call with no diagnostic; the version word turns
that into a refusal at `exec` that names the mismatch. Reliability over time
discriminates.

The acceptance bar under H (installs from Rocky 8's RPMs and passes its own
test suite, counted beside N's and not summed) is the reasonable default
of tier 7; the numbers themselves stay the operator's blanks, as they were.

## Consequences

`bin/check-design-links` reads `; superseded by 0103` from the four index
cells and stops requiring a Settled-by line for them. `Architecture.md`
§ The loader is rewritten to the ratified design and cites DR-0027 and this
record. `Requirements.md` and `target-definition.md` gain Settled-by lines.
Phase 3's toolchain half builds the rebuilt glibc's `ld.so` against the
gate and emits the note; the kernel's `exec` reads it. Nothing under
`core/`, `substrate/` or `seam/` changes by this record.
