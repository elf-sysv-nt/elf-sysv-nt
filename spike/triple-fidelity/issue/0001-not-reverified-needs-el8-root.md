# 0001 — not re-verified on 3.6.10: needs the el8 source root

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10); the
committed `results-2026-08-29.txt` was measured in the old root.

This spike could not be re-run in the primary root: its script needs the el8
package set to count against (`--root DIR` or `--dump FILE`), which is not on
this machine, so it exited asking for that input. The audit records it as
NEEDS-INPUT.

Assessment pending the re-run. The spike counts how many el8 packages mishandle
a nonstandard vendor field — a property of the el8 sources and of `config.sub`
matching, not of the Cygwin host runtime — so it is unlikely to be sensitive to
the environment move. That remains a judgement until the count reruns against
the el8 root. Verdict provisional on the real environment until then. Note that
this spike priced DR-0001 rather than gating it, so a changed count would move a
price, not a gate.
