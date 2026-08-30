# ci -- the merge gate

WP-T1's second half. The first half, the fixture corpus and the fuzz
target, lives with the code it exercises in `loader/elf/t/` and landed
with WP-31. This directory is what makes those tests block a merge.

There is no hosted service behind the word "CI" here. The repository
lives on one machine and merges happen in its worktrees, so CI is a git
hook: `hooks/pre-merge-commit` runs `gate.sh` before a merge commit is
created, and a non-zero exit aborts the merge. `gate.sh` runs every
suite registered in `suites.txt` and fails if any fails, a new fuzz
crash first among them. DR-0035 records the shape and why.

The gate insists on the pinned verification root -- Cygwin 3.0.7, the
rhel root -- because that is the environment the plan's done-when names
and a pass anywhere else certifies nothing. Off that root it refuses to
run and says so; `git merge --no-verify` remains as the deliberate
bypass and should stay deliberate.

`install.sh` wires the hook by setting `core.hooksPath` to `ci/hooks`.
Git resolves that path against the top of whichever worktree the merge
runs in and the setting lives in the shared repository config, so one
install covers the main checkout and every worktree. It is idempotent,
refuses to overwrite a hooksPath it does not manage, and
`install.sh --uninstall` removes exactly what it installed.

`t/run.sh` certifies the gate itself in a scratch repository: a merge
goes through when every suite passes, a merge is blocked when a suite
fails the way the fuzz driver fails on a crash, and the installer is
idempotent both ways. It is registered in `suites.txt`, so every merge
re-certifies the gate at a cost of a few seconds.

Adding a suite is one line in `suites.txt`, repo-relative, and the
contract is small: the suite is run as `<suite> -n <count> [-q]` and
exits non-zero on any failure.
