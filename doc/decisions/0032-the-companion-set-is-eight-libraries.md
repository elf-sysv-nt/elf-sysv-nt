# DR-0032 — the companion set is eight libraries

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-54

## The question

DR-0013 mapped nine libraries and left one call open: which of glibc's other
objects — `ld-linux`, the `libnss_*` modules, `libmvec`, `libanl`,
`libthread_db`, the preloadable helpers — join the companion set the veneer
ships beside `libc.so.6`.

## The decision

None of them. The companion set is exactly the eight the plan names:
`libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libcrypt.so.1`,
`libresolv.so.2`, `libnsl.so.1`, `libutil.so.1`.

The line is drawn where the exit criterion draws it: what a vendor binary's
`DT_NEEDED` reaches by name. The excluded objects are reached other ways or
not at all. `ld-linux` is named in `PT_INTERP`, not `DT_NEEDED`, and its role
is the loader's own (WP-31 and its successors), not a library the veneer
fabricates. The `libnss_*` modules are loaded by name at runtime through the
NSS machinery, which is a `dlopen` behind our own libc, so they are runtime
work, not link-surface work. `libmvec` appears in `DT_NEEDED` only when a
binary was compiled with vectorized math, which el8's own distributed
binaries are not; `libanl`, `libthread_db` and the preload helpers
(`libSegFault`, `libmemusage`, `libpcprofile`) are likewise absent from the
`DT_NEEDED` lists the plan's binaries carry. Any of them can be added later
by one row in `libraries.tsv` and a rerun of the extractors, which is the
whole cost of having been wrong.

Operationally the set is not written down twice: `build-companions` reads
`veneer/version-map/libraries.tsv` and builds every soname there except
`libc.so.6`. A library cannot be in the version map without being built, nor
built without being mapped.

## What follows from it

The satisfaction check (`elfneeds.py` against the built tree) certifies nine
names. A vendor binary that turns up needing a tenth — a `libmvec` user, say —
fails the check loudly rather than being half-served, and the fix is a
`libraries.tsv` row plus extraction, not new machinery.
