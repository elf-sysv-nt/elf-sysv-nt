# 0001 — re-verified on Cygwin 3.6.10: verdict holds

Raised 2026-08-30, re-running the red-zone delivery measurement in the primary
Cygwin root (3.6.10, gcc 14) against the committed `results-2026-08-29.txt`
(rhel root, 3.0.7), after the environment moved to the primary root.

Verdict unchanged: the reserving delivery holds the red zone. The re-run reports
`case_altstack=pass` with 2000 deliveries all handled and the red zone whole,
the same shape the committed transcript recorded.

A caveat worth stating: this spike proves the *reserving delivery* holds the red
zone, which is the fix DR-0006 chose. It does not measure the plain fault-under-
a-System-V-frame path, which is what `abi-crossing` (spike 3) finds broken on
3.6.10 — see `spike/abi-crossing/issue/0001`. The two are not in conflict: this
one covers the repaired delivery, that one covers the unrepaired fault crossing.

No action here beyond noting the relationship.
