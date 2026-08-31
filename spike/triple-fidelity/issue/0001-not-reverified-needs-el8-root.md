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

## Re-verified, 2026-08-31 — the count holds exactly

The input was on the machine: the audit missed `/c/-/el8/dump`, the 306 KB
concatenated harvest that `count-vendor-misses.sh --dump` exists to classify
(the el8 README documents it as exactly that). Re-run in the primary root, the
transcript is `results-2026-08-31.txt`, and every summary count is identical
to 2026-08-29's — `packages_affected=32`, `affected_share=1.1%`, the controls
included — so DR-0001's price stands unmoved on the real environment. Two
detail sections of the old transcript (the per-package rejection list and the
literal-vendor file list) render only in `--root` mode and are absent from
the `--dump` re-run; the 2026-08-29 transcript remains the record of those.
Closed.
