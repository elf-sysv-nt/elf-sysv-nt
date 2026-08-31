# Handoff — applying the branch model (proposal 0005)

Session of 2026-08-30/31. This is the first implementation handoff, so it is
numbered `.1`. It records what the session built, what it broke and repaired
along the way, and the exact point the next session starts from.

## What the session did

It applied `doc/proposals/0005-branch-management-for-concurrent-sessions.md` and
recorded the model as DR-0039. The occasion was a recurring pair of collisions:
the trunk kept diverging because sessions committed straight onto it, and two
sessions once drew the same decision-record number, which is how DR-0037 came to
exist twice. Neither is a merge problem. Both are allocation problems, and the
fix is to serialize the two scarce namespaces — the trunk and the id counters —
rather than ask writers to coordinate by eye.

The mechanism now lives in the tree. `bin/session-start <kind>/<slug>` cuts a
branch off `march` into its own worktree; `bin/session-land` merges it back under
the integration lock `a/.integration.lock`, binds any decision-record placeholder
to a real number, normalizes the union-merged logs, runs the doc-reference gate,
fast-forwards `main`, and removes the worktree. `bin/allocate-id` does the
binding, at land time, when the maximum in use is known and cannot shift under
it. `bin/normalize-logs` sorts the decisions index and de-duplicates the ledger,
the hold list, and the blocker log after a union merge. The four append-mostly
logs carry `merge=union` in `.gitattributes`, so two branches that both append
survive the merge instead of conflicting on unrelated lines.

One guard makes the failure mode unreachable rather than merely discouraged.
`ci/hooks/pre-commit`, active through `core.hooksPath=ci/hooks`, refuses a plain
commit on `march` or `main`; a merge commit carries `MERGE_HEAD` and passes, and
a fast-forward makes no commit and never reaches the hook. Installing it meant
the pre-merge gate would fire on the primary root too, so `ci/gate.sh` moved with
it: DR-0038 had already ruled that the build and certification root is the
primary Cygwin (3.6.10), and the gate was still pinned to the retired 3.0.7 rhel
root. It is now pinned to 3.6.10, and it runs green here — twenty-five unit
checks, a clean fuzz sweep, and the doc-reference gate at 230 of 230.

## What broke, and how it was repaired

The bootstrap commit swept in a stray `runtime/coredump/t/core.elf` through a
careless `git add -A`. It is a generated coredump fixture, not history. The fix
removed it from the commit, added `runtime/coredump/t/.gitignore` so `run.sh`
cannot re-stage it, and rewrote `march` to a clean tip. That rewrite is the only
reason `main` had to be reset by hand rather than fast-forwarded: the operator
ran `git reset --hard march`, and the two branches have tracked each other since.

The land pipeline was exercised end to end before any of this was trusted. A
throwaway session authored an `XXXX` placeholder record, and `session-land` bound
it to DR-0040, normalized, gated, fast-forwarded `main`, and tore down the
worktree. That test was then reverted, which is where the `core.elf` sweep was
caught. The trunk guard was proven the same way: a probe commit onto `march` came
back refused.

## State at handoff

`main == march == 15e1762`. The worker's SKILL.md is rewritten to build on a
`session-start` branch and land through `session-land`, retiring its old
hand-rolled merge into `march`; it remains disabled. Proposal 0005 is marked
accepted, and this document goes in through the mechanism it describes, which is
the fourth real session to land that way.

The operator has already begun the environment-switch spike work that the model
was cleared to unblock. Two characterizations have landed — `map-and-jump` on the
3.6.10 overlap placement, and `abi-crossing` on the fault-through failure, with a
follow-up that gives the fault cases a specimen gcc 14 cannot elide. Both came in
through `session-land`, and `main` fast-forwarded each time without a divergence
to untangle, which is the whole point of the exercise.

## Where the next session picks up

The four reopened work packages — WP-22, WP-32, WP-43, WP-61 — stay in
`doc/status/hold.txt` until their governing spikes conclude and the runtime is
made to converge on 3.6.10; that is the redo in the plan, not a rebuild, since
nothing built was lost. Five input-blocked spikes still need their inputs before
they can be re-verified on the primary root. `doc/test-environment.md` still
names the rhel root in places that DR-0038 retired, and wants a reconciling pass.
The worker can be re-enabled for packages none of the held work touches once
someone is watching the first automated land. One loose end sits in the tree: a
leftover `a/wt/…-wp26` worktree from the earlier winsup build, which the rhel-root
question may have invalidated anyway, left in place for the operator to judge.

Two constraints held through the session and still hold. The held work packages
are not to be redone until their spikes finish, and the spikes are the operator's
to run in their own sessions. Applying the branch model touched neither.
