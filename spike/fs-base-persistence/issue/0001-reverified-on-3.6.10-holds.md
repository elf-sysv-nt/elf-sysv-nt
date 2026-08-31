# 0001 — re-verified on Cygwin 3.6.10: verdict holds

Raised 2026-08-30, re-running `measure-fs-base-persistence.sh` in the primary
Cygwin root (3.6.10, gcc 14) against the committed `results-2026-08-29.txt`
(rhel root, 3.0.7), after the project's build and test environment moved to the
primary root.

Verdict unchanged: a user-written `%fs` base does not survive a context switch
(the re-run reports "the base does not survive", the same `no` the committed
transcript recorded). The spike's non-zero exit is that `no` verdict, not a
regression.

This is expected: the spike measures a Windows behaviour reached through Cygwin,
not a Cygwin-version-specific one, so it is insensitive to the 3.0.7-vs-3.6.10
move. No action; recorded so the environment switch has a per-spike trail.
