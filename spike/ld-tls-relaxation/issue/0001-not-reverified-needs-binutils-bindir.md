# 0001 — not re-verified on 3.6.10: needs the binutils bindir

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10); the
committed `results-2026-08-29.txt` was measured in the old root.

Re-running `measure-relaxation.sh` in the primary root without arguments failed
with "the link failed": the script wants the target's binutils named explicitly
(`-B <bindir>`, the WP-12 `ld` built for the triple), and the bare invocation did
not find a usable linker. The audit records it as NEEDS-INPUT rather than as a
divergence — the failure is a missing argument, not a changed verdict.

Assessment pending a re-run with `-B /c/-/x-elfsysvnt/bin`. What this spike
measures is whether `ld` emits `%fs`-relative TLS sequences on its own — a
property of the binutils build (WP-12), not of the Cygwin host runtime — so the
move should not change its verdict, but that must be confirmed by pointing it at
the toolchain and rerunning. It feeds WP-12's refusal of the TLS relocations;
its verdict is provisional on the real environment until re-run.
