# DR-0057 — the acceptance embryo credits a certified shim, not only a filled stub

Accepted 2026-09-01. Source: WP-56, the acceptance harness after the last slice crossed.

## Context

The acceptance embryo (`acceptance/accept.sh`) reads a built package's undefined
libc symbols and sorts each into what the runtime has behind it: forward, shim,
stub, or — since DR-0052 — filled, a bucket-4 stub the wiring layer answers with
a synthesized, certified body. A package reads `ready` when nothing it imports
is a bare shim, stub, or unclassified name. Only then is running its test suite
the next thing to try.

The harness drew the filled distinction one bucket too narrowly. It credited a
bucket-4 stub named in a `*-filled.tsv` manifest, but treated every bucket-3
shim as unwritten, no matter what the wiring layer had done with it. That was
right while the slices were unwired. It stopped being right the moment they were
not.

bzip2, the first pinned leaf, imports forty symbols. Five are bucket-3 shims:
`__errno_location`, `__lxstat64`, `__xstat64`, `open64`, and `signal`. By
2026-09-01 all twenty-four wiring slices were written and live-crossed against a
real el8 userland — the string, filesystem, and signal slices these five belong
to among them. Their translations exist and are certified. Yet the harness still
read bzip2 `needs-wiring` and printed "waits on WP-56 to wire these slices," a
sentence that had become false: WP-56 had wired them. The verdict lagged the
tree because the harness had no way to see a shim as done.

A written shim is the same kind of thing as a filled stub. Both are bucket
members Cygwin's export surface does not satisfy by name; both have a body the
project wrote and certified standing behind them; both let a caller through. The
filled stub earned a manifest and a disposition of its own. The written shim had
neither.

## Decision

A bucket-3 shim reports as `wired` when its slice has been live-crossed, and as
`shim` otherwise. The gate is the crossing, not the wiring: the harness reads the
union of `wire-<slice>.shims.tsv` over exactly the slices that carry a
`veneer/wiring/t/live-<slice>.sh`, and a shim in that set has a translation a
differential against real glibc has already certified. A shim whose slice is
written but not yet crossed stays `shim` and still blocks — a translation no
crossing has checked is a promise, and the harness does not credit promises.

`ready` now means every imported symbol forwards, is a wired shim, or is a filled
stub — every name has a certified body behind it. `classify.awk` gains the
`wired` disposition beside `filled`, gated on a certified-shim manifest passed
the same way the filled manifest is. On this rule bzip2 reads `ready`: 34
forward, 5 wired, 1 filled.

This does not touch what `ready` has always withheld. Running the package's test
suite — WP-56's overall done-when — still needs the loader's dynamic-exec path to
stand in for `ld-linux` and resolve `libc.so.6`, which is the loader's surface
and not the harness's. `ready` says the veneer is complete for this package, not
that the package has run.

## Consequences

The acceptance verdict tracks the wiring ledger instead of lagging it: as each
slice crosses, the shims it covers turn from `shim` to `wired` on the next run,
with no edit to the harness. A regression that un-crosses a slice — removes its
`live-*.sh` — turns its shims back to `shim` and drops a dependent package out of
`ready`, which is the intended alarm.

The `wired` count is not the same claim as `forward`. A wired shim runs a written
translation; a forward resolves to an export directly. The verdict line and the
`key=value` record both carry `wired` as its own field so a reader can tell the
veneer's own work from what Cygwin already supplied.

`ready` remains necessary, not sufficient, for WP-56. The package still has to
run its suite and pass. That step waits on the loader, and this decision neither
advances nor forecloses it.
