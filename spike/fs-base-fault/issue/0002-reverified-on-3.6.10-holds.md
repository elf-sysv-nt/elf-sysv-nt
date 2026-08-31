# 0002 — re-verified on Cygwin 3.6.10: verdict holds

Raised 2026-08-30, re-running `measure-fs-base-fault.sh` in the primary Cygwin
root (3.6.10, gcc 14) against the committed `results-2026-08-29.txt` (rhel root,
3.0.7), after the environment moved to the primary root.

Verdict unchanged: an access through a zeroed `%fs` base faults, and a handler
resumes from it, over the data-movement forms and not the arithmetic ones. The
re-run reports `faults_refused=1` consistent with the committed transcript.

No action. This fault path — a `%fs`-relative access with no valid base — is not
the same as the fault-under-a-System-V-frame delivery that `abi-crossing`
(spike 3) finds broken on 3.6.10; this one still behaves as recorded. Noted so
the environment switch has a per-spike trail. (Issue 0001 tracks a separate,
pre-existing debt of this spike.)
