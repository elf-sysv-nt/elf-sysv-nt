# 0001 — its re-verification is now WP-26 itself

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10); the
committed `results-2026-08-29.txt` was measured in the old root.

The audit's automatic pass found no single regenerate script here to re-run (the
directory holds `prerequisites.tsv`, probe sources, and the reserving-delivery
patch rather than one `measure-*.sh`), so it was skipped by the sweep. But the
substantive re-verification of this spike is not a script — it is WP-26 itself:
"can this machine build `cygwin1.dll`, and does a reserving delivery hold" is
exactly what building winsup into `elfsysv1.dll` now answers, for real, in the
primary root.

That is the right place to answer it. The spike ran on 3.0.7 and its first
question — can the machine build it — is one the rhel root's gcc 7.4 could not
have answered for the 3.6.10 source anyway. WP-26 builds against Cygwin 3.6.10
with gcc 14, which is the environment the runtime actually ships from. The
second question — does a reserving delivery hold — is re-measured by
`spike/redzone-delivery` (holds on 3.6.10, see its issue 0001) and by WP-43's
signal certification, which currently fails on the fault-through path
(`spike/abi-crossing/issue/0001`). So this spike's answer is being re-derived by
WP-26 and WP-43 on the real environment rather than by a standalone re-run.
