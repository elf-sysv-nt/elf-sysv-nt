# DR-0035 — CI is a pre-merge gate on the pinned root, not a hosted service

Status: accepted  ·  ratified 2026-08-30 (DR-0036)  ·  superseded on the CI-root point by DR-0037
Date: 2026-08-30
Deciding: the WP-T1 author, applying the plan's done-when to the
repository as it actually runs
Proposal: none; taken when WP-T1 had to make "runs in CI" concrete

## What was decided

"CI" in this repository means `ci/gate.sh` run by a `pre-merge-commit`
hook, on the pinned 2019 Cygwin — the rhel root, 3.0.7, gcc 7.4. The
gate runs every suite registered in `ci/suites.txt` and a non-zero exit
from any of them aborts the merge before its commit exists. That is the
whole mechanism: no service, no runner farm, no badge.

The gate refuses to certify anywhere but the pinned root. On another
root it exits with a message naming the bypass, `git merge --no-verify`,
so that skipping the gate is always a decision someone made rather than
an accident of which shell was open.

## Why a hook rather than a service

The plan's done-when for WP-T1 asks for two properties: the suite runs
on the pinned 2019 Cygwin, and a new crash blocks a merge. A hosted
service has neither. The pinned root is one machine's frozen
installation, not something a cloud runner can be handed, and merges
happen in this repository's worktrees, where a hook can actually stand
between a failing suite and a merge commit while a remote status check
can only advise. Wiring the gate where the merges are gives the blocking
property by construction.

The cost is honest: the gate only guards merges made where the hook is
installed, `--no-verify` walks past it, and nothing guards a plain
commit or a fast-forward. Those are accepted. The worker that makes
most merges runs on the pinned root with the hook installed, and the
bypass leaves a decision where automation would have left a gap.

## The registry

`ci/suites.txt` is the list of what a merge must survive. It opens with
the WP-31 parser suite — corpus verdicts and the fuzz target — and the
gate's own certification. Suites added by later packages join it one
line at a time, and an empty registry fails the gate outright, because
a gate that runs nothing and passes is worse than no gate.

## What it does not decide

Nothing here fixes fuzz depth per merge (the gate's default is two
million cases, an option either way), and nothing prevents a hosted
service later if the repository ever grows one; that would be a new
record pointing back here.

## Where it is written down

`ci/README.md` carries the operating description, `ci/install.sh` the
wiring, and `ci/t/run.sh` the proof: in a scratch repository, a passing
gate admits a merge, a crashing suite blocks one, and the installer is
idempotent both ways.
