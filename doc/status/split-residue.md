# What the split left unfinished

Written 2026-09-05, against the filtered tree at the commit that adds this
file. The repository was cut from the veneer arc by `git-filter-repo`, and
the cut is clean where it is mechanical: the registries, the decision index
and the spike ledger were reconciled with what carried, `substrate/run.sh`
certifies substrate N 9/9 against its written bar, and five of the seven
checkers pass.

Two do not, and they fail for the same reason. Prose written for the veneer
still points at things that stayed behind.

## The counts

`bin/check-doc-refs` reports 106 problems over 399 references, two of which
are this file naming the two documents it says did not carry.
`bin/check-design-links` reports 24, all of them citations to records the
filter dropped.

The classes, largest first:

- **`doc/milestones.md`** carries rows for the 23 veneer spikes and cites
  their transcripts. Roughly half the total.
- **`veneer:doc/IMPLEMENTATION-PLAN.md` and `veneer:doc/Next-Steps.md`** did not carry, and
  a dozen decision records cite one or the other in their header block. The
  records are otherwise live.
- **`AGENTS.md`, `doc/design/Architecture.md`, `Address-Space.md`,
  `Requirements.md`, `ROADMAP.md`, `doc/status/not-verified.md`** cite the six
  archived veneer design documents, the dropped proposals, or dropped spikes.
- **`doc/design/Verification-Plan.md`** cites DR-0079 and DR-0085, both
  retired with the arc.

None of it is load-bearing. Nothing here is compiled, run, or depended on by
the substrate suite; every failure is a sentence pointing somewhere the
sentence can no longer reach.

## Why it was not fixed here

Fixing it is not repair, it is authorship. `Architecture.md` describes a
veneer over a re-faced Cygwin and has to be rewritten for a kernel at the
syscall boundary; `milestones.md` has to decide whether a row for a spike that
now lives in the sibling stays as history or goes; the records citing
`IMPLEMENTATION-PLAN.md` need either that document reconstituted for this arc
or their headers repointed. Proposal 0011 settles the design those documents
must come to describe. It does not settle these, and guessing them inside a
filter run would bury real decisions in a mechanical commit.

So they are counted, named, and left. `doc/history/the-veneer-arc.md` is what
a reader follows in the meantime: a citation this tree cannot resolve is
pointing at the sibling repository, not at a mistake.

## The spike ledger

`test/t3-regen.sh` reruns 21 rows and regenerates 17. What it does not:

- **`fs-base-persistence`** flaps. A different case fails each run — `apc` once,
  `apc` and `syscall` the next — and it flaps identically in the veneer
  checkout, which the split did not touch. That is the instrument bending, not
  a finding that moved, and the contract says to believe a FAIL only from an
  unloaded host: three runs here gave three answers. It wants rerunning on a
  quiet machine before anyone reads anything into it.
- **`triple-fidelity`** loses one progress line, `count-vendor-misses:
  classifying`, from an otherwise identical transcript. Also identical in the
  veneer checkout, so it predates the split by some days.
- **`demand-census`** no longer reports at all. Its two analysis rows are
  retired: they wanted the untracked census annex *and* two veneer
  classification tables that left with the arc, and both questions they answer
  are veneer-framed. `census.py`, the collection half, still runs anywhere.

Two defects were found and fixed getting there, both of which would have hit
the first person to clone this repository. `spike/versioned-libc` was dropped
by the filter although two carried spikes execute its `rpmx.py`; it is carried
now. And the runner picked a spike's transcript by mtime, which a fresh clone
flattens, so it silently diffed against a superseded transcript — caught on
`ld-tls-relaxation`, where the older of two transcripts records the opposite
verdict.

## The other gate

The publish-gate survey was re-counted against this tree and then acted on.
The host name and the user name were ruled inconsequential. The paths were
not, and DR-0096 is what came of that: three named roots, resolved rather than
written out, with `bin/check-roots` holding the line. What remains is the
operator's call on which address the commit history should carry, since
changing it across 353 commits is a rewrite and cheaper before a push than
after one.
