# DR-0107 — the documentation gate reads the whole tree, and says what it cannot resolve

Status: accepted
Date: 2026-09-06
Deciding: the operator, on the recommendations of 2026-09-06
Proposal: none
Amends: doc/design/Verification-Plan.md § What the registry must carry

## What was decided

`bin/check-doc-refs` resolves a cited path under any top-level directory the
tree tracks, taken from `git ls-tree --name-only HEAD`, where it resolved only
`doc/`, `bin/`, `spike/` and `ci/` before. Two citations that are correct and
still unresolvable are exempt by name in the checker, each with its reason
beside it. A fourth check joins the three: every criterion
`Verification-Plan.md` calls met names a harness that exists and carries a row
in `test/suites.tsv`. `bin/t/check-doc-refs-test.sh` is the checker's own bar,
nine injected defects it must refuse, and it gates.

The proposals stay skipped. `doc/design/proposals/` and
`doc/history/design-gaps/` are exempt from the path check as they were.

## Why

Two defects landed through this gate in three weeks, and neither was the gate
failing at what it did. DR-0106, written on 2026-09-06, cited a GCC patch and two files under
toolchain/glibc that exist only on a branch; a reader who cloned the
repository and followed the record found nothing. The allowlist is the whole
reason: `toolchain/` was not one of the four directories, so those citations
were never examined. Records cite
`core/`, `substrate/`, `test/` and `toolchain/` constantly — DR-0106's
Consequences paragraph alone had four — and the gate had never looked at any
of them.

Taking the set from git rather than from the filesystem settles two things
that a hand-maintained allowlist would have got wrong.

`a/` is the untracked session annex. It exists in a working checkout and not
in a fresh clone, so a filesystem listing makes this gate's verdict depend on
whether anyone had run a session in the tree: green in CI, red under
`bin/session-land`, which is the one place it runs for real. DR-0039's
`a/.integration.lock` is the citation that would have failed. From `HEAD` the
annex is simply not a directory of the tree, and the question does not arise.

The veneer arc retires for free. `veneer/`, `loader/` and `runtime/` are
absent from this repository by construction (`Architecture.md`,
`doc/history/the-veneer-arc.md`), so they are not directories of `HEAD`, so
the eighty-odd citations of them in the retired records are never asked to
resolve. That matters more than the convenience: those records are
append-only, and a gate that demanded they resolve would have forced them to
be edited into agreement with the present, which is the one thing the
decisions index forbids. No exemption list was needed for any of them.

What did need exempting is the pair that survives both rules. DR-0010, itself
superseded in framing by DR-0000, describes in the past tense a WP-14 draft
under `toolchain/sysroot/include/` that WP-15 deleted — a retired path under a
carried directory, which no prefix rule can distinguish from a live one.
DR-0034's `etc/elfsysvnt/manifest` is relative to the root `elf-install`
installed into, not to this repository, and a fresh clone is right not to have
one. Both are exempt by name, each carrying its reason, because a reader who
meets one of these later is owed the argument rather than a bare entry.

The fourth check exists because "criterion N is met" is a claim about today
and the path check cannot reach it. Proposals are skipped for a good reason: a
proposal names artifacts it proposes to create, and 0011's criteria 4 to 17
are promises. But 0011 § 7 named test/t/hello-static.sh for criterion 1, at
the gate tier, and that file has never existed — `core/run.sh` did the work,
at the report tier — and nothing said so for three weeks. Un-skipping the
proposals would have caught it at the cost of seventeen exemptions and a
standing pressure to write proposals in the present tense. Checking the claim
where it is made costs one function and cannot fire on a criterion the plan
has not yet claimed.

Tier 1 throughout for what the checks resolve, since a gate that cannot see
three quarters of the tree's directories is not correct at its own job. Tier 5
for how the exemptions are carried: two entries in the checker with their
reasons beside them, rather than a registry file, because the reason is what a
later reader needs and two rows do not earn a file. `test/suites.tsv` is the
precedent for the registry, and the moment there are a dozen exemptions this
should become one.

## What it costs, and to reverse

The gate examines 473 citations where it examined 396, and a document that
cites a path in `core/`, `substrate/`, `test/` or `toolchain/` must now be
right about it. That is the intended cost.

An exemption is a hole by construction. Two cases in
`bin/t/check-doc-refs-test.sh` disable one each and require the checker to
fail, so an exemption that stops being load-bearing — because the path came
back, or the record was rewritten — is reported rather than silently widening
what the gate ignores.

Reversing is the `topdirs` function and the `EXEMPT` table, both in one file;
nothing on disk and no other tool depends on either.

## Consequences

`Verification-Plan.md` § What the registry must carry states what the gate
tier now holds. `test/suites.tsv` and `ci/suites.txt` carry
`bin/t/check-doc-refs-test.sh` at the gate tier, held one-to-one by
`bin/check-suites` as every gate suite is. Two documents were corrected rather
than exempted when the widened check found them: `Verification-Plan.md` no
longer writes the never-built harness name as a citation, and DR-0106 names
its branch in prose instead of as a path.
