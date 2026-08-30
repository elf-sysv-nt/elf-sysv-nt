# DR-0036 — ratification sweep of DR-0008 through DR-0035

Status: accepted
Date: 2026-08-30
Deciding: the operator, who directed the sweep and delegated the per-record
ladder check; the ratification is the operator's act
Proposal: `doc/design-gaps/proposal.md`, finding F7

## What was decided

Every decision record from DR-0008 through DR-0035 is ratified. Each was run
against `doc/decision-ladder.md`; each records a decision the ladder reaches
before tier 8 — correctness-first or settled on a measurement, none a guess,
each naming the basis it was taken on. None is reopened.

The records DR-0000 through DR-0007 are the operator's own and were never
provisional; they are outside this sweep and unchanged by it. DR-0008 onward
were taken by implementing agents as defensible calls, and this record is where
they stop being provisional under the convention `AGENTS.md` now states.

## Per record, with the tier that carried it

- DR-0008 — ratified. Tier 1: mapping through the runtime's `mmap` is the only
  shape that keeps the memory bookkeeping complete; refusing a granule-sharing
  object is tier 3 on top.
- DR-0009 — ratified. Tier 1: the signature-agnostic tail jump is ABI-correct
  and leaves translation at the one site that has the types.
- DR-0010 — ratified. Tier 1, and superseded in framing by DR-0000: the header
  is el8's, vendored verbatim, which is the fidelity the whole project rests on.
- DR-0011 — ratified. Tier 1: an own cache format owes nothing to glibc's
  private layout; validating every offset before dereference is tier 3.
- DR-0012 — ratified. Tier 1: the convention boundary and the unwind boundary
  are one line through the entry point. Its tripwire is carried into WP-27,
  where compiling all of winsup must hold it; that is future verification, not
  a defect in the decision.
- DR-0013 — ratified. Tier 1: the companion provenance is a fact about where
  the version nodes live, established not chosen.
- DR-0014 — ratified. Tier 1: of Windows' two page numbers the commit size is
  the correct `AT_PAGESZ`; the value is measured, not hardcoded.
- DR-0015 — ratified. Tier 1: rebuilding the Microsoft `va_list` is what makes
  the variadic ABI correct across the boundary.
- DR-0016 — ratified. Tier 1, with tier 5: certifying against vendor objects is
  the only correct evidence when the toolchain cannot emit the relocations.
- DR-0017 — ratified. Tier 1: a version-node identity object is not a symbol,
  so a fifth disposition is the faithful classification.
- DR-0018 — ratified. Tier 2: inheriting Cygwin's counter and enforcing it from
  the first release is what sustains compatibility over time.
- DR-0019 — ratified. Tier 1: a separate lookup engine with one versioning seam
  is correct and keeps the later consolidation open.
- DR-0020 — ratified. Tier 3: fixed compiled thunks with no runtime code
  generation are the robust bridge; tier 1 correctness is met either way.
- DR-0021 — ratified. Tier 1: the carrier at the floor of a runtime-owned stack
  is the placement that survives a context switch, measured against `_my_tls`.
- DR-0022 — ratified. Tier 1: the SVr4 five-field prefix through `DT_DEBUG` is
  what the triple's gdb can read.
- DR-0023 — ratified. Tier 1: reproducing glibc's observable rule from the spec
  is both correct and the licensing-clean path (DR-0004).
- DR-0024 — ratified. Tier 1: the TLS layout and DTV from the psABI and the
  generic ABI reproduce the observable behaviour without lifting LGPL code.
- DR-0025 — ratified. Tier 1: a defined constructor order under a cycle is
  correctness a program can otherwise crash on run to run.
- DR-0026 — ratified. Tier 1: naming every symbol in the version script rests
  on a measurement, not a preference.
- DR-0027 — ratified. Tier 1, with tier 3 for the four-hop bound. The
  `BINPRM_BUF_SIZE` constant is a parameter the record itself made cheap to
  change; F8 confirms it by a `git show` against the el8 kernel, through that
  same mechanism. The decision stands regardless of the number.
- DR-0028 — ratified. Tier 1: a non-PIE image has one address, and spike 2
  measured that the parent must reserve it into a suspended stub.
- DR-0029 — ratified. Tier 1, with tier 3: the child re-derives what crossed
  and refuses a mismatch rather than trusting the copy.
- DR-0030 — ratified. Tier 1: the receiving thread builds the frame and returns
  by `iretq`, both measured in `runtime/signal/t/`.
- DR-0031 — ratified. Tier 1: status in a tracked ledger, the worker driven
  from the plan, is what stops the false stops and false completes.
- DR-0032 — ratified. Tier 1: the companion set is exactly what `DT_NEEDED`
  reaches, a closure computed not guessed.
- DR-0033 — ratified. Tier 5: an ELF core leaves the link map, symbols and
  thread state where the triple's gdb reads them, which a minidump does not.
- DR-0034 — ratified. Tier 2, with tier 3: a single on-disk manifest is the
  idempotent installer's memory, and it is read as hostile every time.
- DR-0035 — ratified. Tier 2, with tier 5: a pre-merge hook on the pinned root
  certifies where the target actually is, and names the bypass when refused.

## What this does not touch

The bodies of the ratified records are unchanged; ratification is recorded by a
one-line note on each record's status line pointing here, and by this record in
the index. Reopening any of them remains the new-record mechanism the index
already mandates. The two follow-ups named above — DR-0027's constant and
DR-0012's tripwire — are tracked in their own work (F8 and WP-27) and are not
reasons this sweep withheld ratification.
