# Wiring the bodies, in slices (WP-56)

Work in progress. The forwards become real resolutions into `elfsysv1.dll`
and the shims become translations through WP-55's tables, sliced by
subsystem, with the slice order taken from spike 12's demand ranking.

## Slice machinery

`cut-slices.py map` scans the el8 headers in `veneer/include` with the
cross compiler's `-aux-info` and writes `symbol-slice.tsv`: every declared
function credited to the header that declares it, and through `slices.tsv`
(header -> slice; row order is the attribution priority) to its slice.
The current map covers 1757 functions across 93 headers.

`cut-slices.py order` joins the census `demand-ranking.tsv` against that
map and writes `slice-order.tsv` plus one per-slice worklist, ranked by
package demand. Symbols the map does not know land in an `unassigned`
slice rather than disappearing. `t/run-tests.sh` exercises both halves
against fixtures, network-free and cross-toolchain-free.

## Status

The census (spike 12) is running in the background over the 4855-package
el8 worklist (resumable, logged to `a/build-logs/wp56-wiring-bodies.log`).
When it completes, `census.py report` produces `demand-ranking.tsv`,
`order` cuts the slice queue from it, and the first slice's wiring
begins. No slices cut yet.
