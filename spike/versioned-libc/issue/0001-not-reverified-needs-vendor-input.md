# 0001 — not re-verified on 3.6.10: needs the vendor input

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10), and the
committed `results-2026-08-29.txt` was measured in the old root.

This spike could not be re-run in the primary root: its regenerate script needs
the vendor material (el8's `elfdeps` reading a `Requires` off a synthesized
`libc.so.6`) that is not present on this machine, so the audit reports it as
NEEDS-INPUT rather than as holding or diverged.

Assessment pending the re-run. What this spike measures is `rpm`/`elfdeps`
behaviour over a synthesized library — a static-analysis pipeline rather than a
Cygwin-host runtime behaviour — so it is unlikely to be sensitive to the
3.0.7-vs-3.6.10 move the way the fault-delivery and mmap spikes are. But
"unlikely" is a judgement, and the point of re-verifying is to replace it with a
measurement. Verdict is provisional on the real environment until the vendor
input is available and the script reruns.
