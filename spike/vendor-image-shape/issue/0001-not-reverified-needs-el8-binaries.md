# 0001 — not re-verified on 3.6.10: needs the el8 binaries

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10); the
committed `results-2026-08-29.txt` was measured in the old root.

This spike could not be re-run in the primary root: `measure-shape.sh` reads the
shape (`EI_OSABI`, `.note.ABI-tag`, `PT_LOAD` alignment, SONAME) of el8's own
binaries, and those pinned binaries are not on this machine (`-D <dir>` was not
satisfiable), so the audit records it as NEEDS-INPUT.

Assessment pending the re-run. This is a static read of el8 ELF files with
`readelf`; nothing about it touches the Cygwin host runtime, so it is the least
likely of the spikes to be sensitive to the 3.0.7-vs-3.6.10 move — the bytes it
measures are el8's, fixed, and the same tool reads them under either root.
Verdict provisional only in the formal sense until the binaries are refetched
and the script reruns. This spike feeds `doc/target-definition.md`'s six values.
