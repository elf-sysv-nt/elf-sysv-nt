# 0001 — re-verified on Cygwin 3.6.10: verdict holds

Raised 2026-08-30, re-running the `%gs` thread-pointer measurement in the primary
Cygwin root (3.6.10, gcc 14) against the committed `results-2026-08-29.txt`
(rhel root, 3.0.7), after the build and test environment moved to the primary
root.

Verdict unchanged: every available `%gs` carrier passes, the same result the
committed transcript recorded ("every available carrier passes; read the
per-carrier table"). Carrier C3, the one DR-0003 took, still holds on 3.6.10.

No action. The thread pointer reached through the `%gs` chain is unaffected by
the environment move; recorded so the switch has a per-spike trail.
