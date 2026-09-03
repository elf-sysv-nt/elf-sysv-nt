# DR-0084 — every check carries a written designation, and the gate matches it

Status: accepted 2026-09-03
Date: 2026-09-03
Amends: AGENTS.md § Testing
Deciding: the operator, on 2026-09-03, asking whether more checks existed that
nothing ran and how a new one would be kept from joining them
Proposal: none; taken when an audit found 38 of 45 suites named by nothing

## What was decided

`test/suites.tsv` lists every suite and every checker in the tree, one row
each: the path, a tier, the input it needs, and a note saying what it is for.
`bin/check-suites` holds that registry closed against the tree in both
directions and holds its `gate` tier one-to-one with `ci/suites.txt`.

The tiers are `gate` for the fast, offline, deterministic checks a merge runs,
`report` for those needing a toolchain, a build product or a live el8, and
`standalone` for the few that must never be automated. There is no default:
a check with no row fails the gate.

## What the audit found

The tree holds 45 top-level suites. `ci/gate.sh` ran seven. `bin/nightly-full`
ran one of the others plus the acceptance harness. The worker is agent-driven
and runs what an agent remembers to run. The remaining 38 were named by
nothing at all — not the gate, not the nightly, not any script in the tree —
and ran only when a person typed the path.

Two of them were red on the trunk the day this was written. Neither had rotted.
`runtime/exports/t/reproduce.sh` refuses because the newlib-cygwin checkout sits
at a different ref than the pin, and `runtime/imports/t/reproduce.sh` because
the extractor parses no imports from the DLL it is pointed at. Both are input
state rather than defects, both have been that way for an unknown length of
time, and nothing anywhere reported it.

Three directories hold certifications under names no convention anticipated:
`runtime/face/t/` with fifteen scripts and no driver, `runtime/winsup/t/`, and
`veneer/t/`. Running "the face suite" means knowing fifteen filenames.

## Why a list would not have been enough

A list is what rotted. `ci/suites.txt` is a list, and it was accurate about the
seven it named and silent about the thirty-eight it did not. What makes this
registry different is that it is closed, and that closure is checked:

A check in the tree with no row fails, so the author of a new suite writes its
tier and its purpose at the moment of writing rather than leaving it for
somebody to notice. A row naming a path that is gone fails. A `t/` directory
holding scripts and no registered entry point fails, which is what catches a
runner named outside the convention — the case that hid the face suite. And the
`gate` tier and `ci/suites.txt` must agree exactly, so the registry cannot
claim a suite gates while the gate does not run it, or the reverse.

Every one of those was watched failing before this landed: a new suite added
with no row, a runner named `verify-it.sh`, and a gate row removed from
`ci/suites.txt`. Each went red naming the specific problem, and green again on
repair.

## Why there is no default tier

A default is how a thing stops being thought about. Had an unregistered suite
defaulted to `report`, the registry would have filled itself and nobody would
ever have decided whether any of it should gate — which is the state this
record exists to leave.

## Why an absent input is not a failure

`test/t3-regen.sh` already draws this distinction for spikes and its reasoning
carries: a suite that cannot run because a toolchain or a vendor dump is absent
was not checked, and reporting that as a pass would be a lie while reporting it
as a failure would train the reader to ignore red. The nightly counts the three
states separately. The two red checks above are honest failures on inputs that
are present but wrong, which is a third thing again, and the notes say so.

## Consequences

`ci/suites.txt` gains `bin/check-suites` and `veneer/xlat/t/reproduce.sh`, and
loses `veneer/libc/t/run-tests.sh`. That last is a correction rather than a
retreat: it builds `libc.so.6` and so needs the cross toolchain, and a gate
suite depending on one machine's build products blocks every merge taken
anywhere else. It is tier `report` and the nightly runs it.

`bin/nightly-full` runs the report tier and prints passed, failed and
not-checked separately.

`AGENTS.md` § Testing states the rule where an author of a new check will meet
it.

## Not verified

That the tier assignments are right. They are a first pass taken from what each
suite appears to need, and several `needs` paths are stated from the suite's
behaviour rather than from reading its source. The nightly reporting
not-checked for something that would have run, or running something slow enough
to matter, is the correction, and it costs a row's edit.

That the report tier passes. Nothing has run all 59 of them in one go; two are
known red and one, `runtime/varargs`, hits a host gcc ICE recorded in the
blocker log since 2026-08-31. The first full nightly is the measurement, and it
should be read as an inventory rather than as a regression.
