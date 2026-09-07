# DR-0109 — session worktrees live beside the repository, and the shared checkout is asked of git

Status: accepted
Date: 2026-09-07
Deciding: the operator, on the recommendations of 2026-09-02
Proposal: none
Amends: doc/design/test-environment.md § Where the environment is named

## What was decided

`bin/session-start` creates a session's worktree at
`<parent>/.worktrees/elf-sysv-nt/<YYYY-MM-DD.HH-MM>-<slug>`, one level above
the repository, and `bin/session-land` keeps the integration worktree at
`<parent>/.worktrees/elf-sysv-nt/integration`. Neither is inside the tree any
more. For this checkout that is `/c/-/repo/.worktrees/elf-sysv-nt/`.

This amends the location clause of DR-0039 and nothing else in it: one trunk,
one worktree per session, land under the lock, all stand.

`bin/roots.sh` and `bin/roots.py` gain two names beside the three DR-0096
gave them. `ELFSYSVNT_MAIN` is the shared checkout, obtained from
`git rev-parse --git-common-dir` and valid from inside any worktree wherever
it sits; `ELFSYSVNT_WT_ROOT` is the directory above. Every tool that needs the
shared checkout reads `ELFSYSVNT_MAIN`: `session-start`, `session-land`,
`build_status.py`, `progress.py` and `refresh-status-reports.py` did it by
splitting their own path on the literal `/a/wt/`, and that form is gone.
Stamps in worktree names take the readable `2026-09-07.05-42` shape.

Worktrees made before this record sit under `a/wt/` until moved with
`git worktree move`; the two forms coexist, since `a/` stays ignored. A
session whose worktree still carries the old scripts lands by rebasing onto
`march` first, so that it lands with these.

## Why

DR-0039 put worktrees under `a/wt/` because `a/` was the ignored place to
hand. Three things followed that were not intended.

A worktree is a checkout, not a working note, and `a/` is defined as the
home of notes. `bin/check-worknote-refs` had to learn that `a/wt` is machinery
while `a/issue` is prose, a distinction that exists only because two unlike
things shared a parent.

A checkout inside the checkout is searched with it. `find` and `grep -r`
from the root descend into every live worktree and report each hit once per
tree; tools that read `git ls-files` were safe, tools that walked the
filesystem were not, and the tree has both. Each worktree carries its own
ignored `a/`, so `a/wt/x/a/wt/y` was a legal path with no floor.

The path split failed silently. `${here%%/a/wt/*}` returns its input
unchanged when the separator is absent, so a worktree created anywhere else,
by hand or by a later tool, made every caller take the session worktree for
the shared one: `session-land` would take the integration lock inside it,
merge into it and fast-forward its `main`. Sixteen scripts in the tree already
used `--git-common-dir`, which has no such case; the five that did not now do.

The sibling root answers the first two outright, and the `elf-sysv-nt` level
under `.worktrees/` keeps two repositories in the same parent from colliding on
a slug. The dot prefix keeps the directory out of a casual listing of
`/c/-/repo`, which the operator asked for. Rehearsed on this host on
2026-09-02 with a detached throwaway: `--git-common-dir` and the basename
both resolve from outside the repository, `git worktree move` to a sibling
path succeeds, and the old string form returns the worktree's own path, as
predicted.

## Not verified

That every caller outside the tracked tree has been found. The scheduled-task
prompt, the operator's `~/bin`, and anything under `etc/` were not searched
from this record; a cron line naming `a/wt/` breaks the way the third point
above describes, without an error.

That the readable stamp sorts with the old one in a mixed listing. It does
not: `2026-09-07.05-42-x` sorts before every `20260906-...` name, since `-`
orders before a digit, so a mixed listing is not chronological. The old names
retire with their worktrees and the mixed period is short.
