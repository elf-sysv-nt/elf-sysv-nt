# Proposal 0005 — branch management for concurrent sessions

Status: accepted 2026-08-30 (DR-0039); mechanism applied
Author: Philip Dye
Date: 2026-08-30
Analysed against: aa7692c on `main`

Drafted at the operator's request. Separate sessions — the autonomous build
worker, operator manual edits, per-spike sessions, ad-hoc fixes — write this
repository at the same time, and today they collide: the trunk diverges, the
decisions index conflicts, and two sessions once drew the same DR number. This
proposes a branch model in which disparate sessions work independently and
integrate without those collisions, by isolating the work and closing the two
places where two writers actually contend.

## Context and scope

The build worker already works the right way for its own packages: a worktree
per work package, on its own branch, merged into an integration branch under a
lock. The trouble is everything outside that path. There are two trunk branches,
`main` and `march`, coupled by a fast-forward; the worker drives `march` while
operator edits land on `main`, so the fast-forward breaks and the two diverge.
Every reconciliation is then a manual merge, and some of them conflict on files
that should never have contended.

The collisions are not general. Cataloguing them:

1. Two sessions write the *same trunk lines*. The decisions index and the status
   ledger are appended to at the end, so two sessions both adding a row conflict
   on that last line even though the additions are unrelated.
2. Two sessions draw the *same serial id*. Decision records, spikes and issues
   are numbered by "one past the highest that exists", and two sessions reading
   the same highest allocate the same next number. This produced two DR-0037
   files on 2026-08-30, one for the licensing exception and one for the
   environment switch.
3. The *trunk is written directly* by more than one session. `main` and `march`
   are long-lived shared branches that sessions commit onto, rather than merge
   into, so they diverge by construction.

Everything else is a symptom of sessions not being isolated from one another. In
scope: a single-trunk model, a worktree-per-session convention with two small
scripts, a union-merge rule for append-logs, and land-time id allocation. Out of
scope: the content of any session's work, and any change to what the worker
builds — this is about how independent work integrates, not what it is.

## Goals and non-goals

Goals. Let any number of sessions run at once without their integrations
colliding. Make the trunk unable to diverge. Make the two append-logs and the id
counters collision-proof rather than merely recoverable. Keep the worker's
existing worktree-and-lock discipline and generalize it to every session.

Non-goals. Preventing a genuine content conflict — two sessions editing the same
real source lines *should* stop and be resolved by a human, and no mechanism
should paper over that. Eliminating merges — merges are the integration point and
are wanted; what is not wanted is a merge that conflicts on unrelated additions.

## The mechanism

### 1. One trunk, written only by merges

Collapse the `main`/`march` split to a single integration branch. Keep the name
`march` for it; `main` becomes a published pointer that is only ever
fast-forwarded to `march` and never committed onto. The rule that does the work
is one sentence: **no session commits to the trunk directly** — not the worker,
not a spike session, not the operator, not an agent. The trunk receives merges
and nothing else.

This is what removes divergence as a category. Two branches diverge only when two
writers commit to the same branch from different bases; if the only write to the
trunk is a merge that starts from the trunk's own tip, the trunk advances
linearly no matter how many sessions ran in parallel. A `pre-commit` hook on any
worktree that has the trunk checked out refuses a direct commit and names the
fix ("start a branch: bin/session-start"), so the rule cannot be broken by
habit.

### 2. A worktree and branch per session

Two scripts, in the `check-target-definition` mould, docopt interface, that
formalize what the worker already does and extend it to everyone:

- `bin/session-start <kind>/<slug>` cuts a branch `<kind>/<slug>` from the
  current `march` tip, creates a worktree at `a/wt/$(date +%Y%m%d-%H%M)-<slug>`,
  and prints its path. `<kind>` is a plain prefix that says what the session is:
  `wp/` for a work package, `spike/` for spike work, `op/` for operator manual
  edits, `env/`, `fix/`. Nothing enforces the prefixes; they exist so
  `git branch` reads as a manifest of what is in flight.
- `bin/session-land` merges the current session branch into `march`, holding the
  integration lock (the build worker's `a/.build-worker.lock`, renamed to a
  general `a/.integration.lock`) so two lands cannot interleave; runs the gate;
  fast-forwards `main`; and removes the worktree and branch. On a conflict it
  stops with the branch intact for a human to resolve, which is the one case
  where stopping is correct.

The operator's manual edits go through the same door: `session-start op/licensing`,
edit, `session-land`. The worker's per-package flow is this same pattern with
`kind=wp`. The work never contends because each session is a separate working
tree on a separate branch; only the brief, lock-serialized land touches the
trunk.

### 3. Append-logs merge by union

A `.gitattributes` at the repo root:

    doc/decisions/index.md    merge=union
    doc/status/delivered.txt  merge=union
    doc/status/hold.txt       merge=union
    a/build-blockers.log      merge=union

`merge=union` tells git to keep both sides' added lines when a file diverged,
rather than raising a conflict. Two sessions each appending a row to the index,
or an id to the ledger, then both survive the merge with no marker. Ordering can
interleave, so `session-land` runs a normalizer over these files after the
merge: sort the index rows by DR number, sort and de-duplicate the ledger and
hold list. Union-merge plus a normalizer turns the most frequent conflict in this
repository into a non-event.

The rule for a file to qualify: it must be append-mostly and line-oriented, where
two independent additions are both wanted. The index, the ledger and the hold
list are exactly that. A file where two sides edit the same existing line is not
a union-merge candidate and is left to conflict, correctly.

### 4. Ids allocated at land, not at creation

The DR-0037 collision was a distributed counter read by two sessions at once. The
fix is to stop letting authors draw the number. A decision record, spike or issue
is authored with a placeholder id — the filename carries the slug and an `XXXX`
where the number goes, and the body refers to itself by slug. `session-land`,
already serialized by the integration lock, assigns the next free number from the
trunk's current state, renames the file, and inserts the index row. Because the
allocation happens inside the lock, against the trunk that no other land is
touching at that instant, two sessions can never draw the same number. The author
never picks a number, so two authors can never pick the same one.

For records that must cross-reference each other before they land, the slug is
the stable handle within a session; the number is bound only when the session
integrates. A `bin/allocate-id <kind>` helper does the max-plus-one lookup and
the rename, and `session-land` calls it.

## Why it holds

The three collisions map one-to-one onto the four parts. Direct-write divergence
(collision 3) is removed by the merge-only trunk (part 1) and isolation (part 2):
sessions never share a branch. Same-line append conflicts (collision 1) are
removed by union-merge (part 3). Same-number collisions (collision 2) are removed
by land-time allocation (part 4). What is left is a real content conflict — two
sessions changing the same source lines — which is rare, is genuine, and is the
one case a mechanism should *not* hide: `session-land` stops and hands it to a
person.

The load is where it belongs. The expensive, long-running part of any session —
the build, the edit, the measurement — happens in an isolated worktree and
contends with nothing. The only serialized moment is the land, which is
sub-second and holds the lock only across the merge, the normalize, and the
fast-forward.

## Migration

Nothing needs rewriting; this is additive and can land in one session.

- Add `.gitattributes` with the four union-merge lines.
- Add `bin/session-start`, `bin/session-land`, `bin/allocate-id`, and the
  trunk `pre-commit` hook.
- Rename `a/.build-worker.lock` to `a/.integration.lock` and point the worker at
  it; the worker's STEP 4 and STEP 5 become `session-start wp/<slug>` and
  `session-land`, which is what they already do by hand.
- Retire the `main`/`march` distinction in the worker `SKILL.md` and in
  `doc/test-environment.md`: `march` is the trunk, `main` is its published
  fast-forward.
- Existing in-flight worktrees keep working; they are already branch-per-session.

The change earns a decision record for the branch model once accepted, with this
proposal as its source.

## Cost, honestly

Three small scripts, one `.gitattributes`, one hook, and a convention. The
convention — never commit to the trunk — is the only thing a human has to hold in
mind, and the hook enforces it. Against that, it removes an entire class of
manual reconciliation that has already cost several sessions, and it closes a
number-collision that silently produced two records with the same id. The worker
already lives most of this; the proposal mostly writes it down and extends it to
the sessions that were outside it.

## Open questions

- Whether `main` stays a separate published pointer or is retired entirely in
  favour of pushing `march`. The published-pointer form is one line in
  `session-land` and keeps the name people expect; retiring it is simpler but
  renames the shared branch. Reserved for the operator.
- Whether the normalizer should also run as a `pre-commit` check on the trunk, so
  a hand-built merge that skipped `session-land` still lands sorted. Cheap; likely
  yes.
- Whether spike and issue numbers want the same land-time allocation as DRs, or
  whether their lower collision rate makes the placeholder ceremony not worth it.
  The mechanism supports both; the default proposed is to allocate all three the
  same way, since one rule is easier to hold than three.

## Verification criteria

1. Two sessions each start a branch, each add a decision record and a ledger id,
   and both land with no conflict; the index ends sorted and carries both
   records with distinct numbers.
2. A direct `git commit` on a trunk worktree is refused by the hook.
3. `git merge` of two branches that each appended to the ledger and the index
   produces no conflict markers (union-merge), and the normalizer leaves both
   files sorted and de-duplicated.
4. `bin/session-land` on a branch with a genuine same-line conflict stops with a
   non-zero exit and leaves the branch and worktree intact for resolution.
5. The worker's build flow runs unchanged through `session-start wp/<slug>` and
   `session-land`, and `main` only ever fast-forwards to `march`.
