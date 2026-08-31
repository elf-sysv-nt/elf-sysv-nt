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

## Re-verified, 2026-08-31 — holds

The input was on the machine after all: the audit's `-D` probe missed
`/c/-/el8/vendor-image-shape`, which holds the pinned tree intact. Re-run in
the primary root against it, `measure-shape.sh` measured the same 41 ELF
files; the transcript is `results-2026-08-31.txt`. The only difference from
2026-08-29 is the reporting tool: the primary root's readelf 2.47 labels 14
of the 41 `DYN` objects "Position-Independent Executable file" where 2.29
called all 41 "Shared object file" — same bytes, finer labels, 27 + 14 = 41.
Every value the spike feeds `doc/target-definition.md` reads identically.
Closed.
