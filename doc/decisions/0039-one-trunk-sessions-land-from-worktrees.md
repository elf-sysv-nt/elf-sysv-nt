# DR-0039 — one merge-only trunk; every session lands from its own worktree

Accepted 2026-08-30. Source: `doc/proposals/0005-branch-management-for-concurrent-sessions.md`.

## Context

Work on this repository happens in several concurrent sessions — the build
worker on its cron, and one or more interactive sessions, sometimes on the same
afternoon. The recurring damage came from two habits that a single-branch model
invites: sessions committing straight onto the integration branch, and two
sessions each reaching for the same next serial number. The first made `main`
and `march` diverge again and again; the operator would commit a decision record
to `main` while a session built one on `march`, and the two had to be untangled
by hand. The second produced the DR-0037 collision, where the operator's
`0037-the-linking-exception-carries-forward.md` and this agent's environment DR
were both authored as 0037 and one had to be renumbered after the fact.

Neither is a merge problem. Both are allocation problems: two writers drawing
from one namespace with no serialization between them.

## Decision

There is one trunk, `march`, and it takes merges and fast-forwards only. `main`
is a published pointer that only ever fast-forwards to `march`; nobody commits
to either directly. A `pre-commit` hook (`ci/hooks/pre-commit`, active through
`core.hooksPath=ci/hooks`) refuses a plain commit on `march` or `main` — a merge
commit carries `MERGE_HEAD` and is allowed, a fast-forward makes no commit and
never reaches the hook.

Every session works on a branch `<kind>/<slug>` in its own worktree, cut from
`march` by `bin/session-start`, and returns through `bin/session-land`. Land is
serialized by the integration lock `a/.integration.lock`, so two lands never
interleave. Land binds serial numbers last, not first: a decision record is
authored as `XXXX-<slug>.md`, and `bin/allocate-id` assigns its real number
inside the lock at land time, when the maximum in use is known and cannot change
under it. The append-mostly logs — the decisions index, the delivered ledger,
the hold list, the blocker log — carry `merge=union` in `.gitattributes` so
independent additions from two branches both survive the merge instead of
conflicting; `bin/normalize-logs` then sorts the index and de-duplicates the
rest, so union's order artifacts do not leak.

This supersedes nothing in substance; it writes down and enforces the branch
model the repository had only by convention. It leaves DR-0035/DR-0038's gate
untouched: the gate still runs on merge into the trunk, now pinned to the
primary root.

## Why this passes the ladder

Correctness and reliability: the collision and the divergence were both defects
that reached the operator's hands. Serializing land and allocating ids under the
lock removes the race that caused them, rather than asking writers to coordinate
by hand. The trunk guard makes the failure mode — a direct commit to the trunk —
impossible to reach by accident instead of merely discouraged. That is the
lowest rung that actually closes the hole, so the ladder stops here.

## Consequences

A session is now a worktree, not a checkout of a shared branch, so two sessions
can no longer step on each other's working tree. The build worker becomes an
ordinary session: it builds on its own branch and lands the same way, which is
why its direct-to-`march` commit step is retired. The cost is one extra worktree
per active session under `a/wt/` and a land step that must hold a lock briefly;
both are cheap on a single machine. Hand edits to the union-merged logs still
work — `normalize-logs` is idempotent and only ever sorts and de-duplicates.
