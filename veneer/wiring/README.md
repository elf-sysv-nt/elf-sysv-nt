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

## The translation core

`gen-xlat.py` turns WP-55's `errno-map.tsv` and `signal-map.tsv` into
`xlat-core.gen.c` / `.gen.h`: four functions (`__esn_errno_up/down`,
`__esn_signal_up/down`) over dense value arrays, the one translation
every down-call wrapper shares. Unclaimed values pass through unchanged.
Where Linux aliases two names onto one value that Cygwin keeps apart
(EDEADLK/EDEADLOCK, ENOTSUP/EOPNOTSUPP), the down direction picks the
side-agreeing value when there is one and otherwise an explicitly named
winner in the generator, never a silent first-row-wins. The generated
files are committed; `t/run-tests.sh` regenerates them, requires
byte-identity, and runs compiled spot checks of both directions.

## The crossing

`gen-wire.py` turns the forward map and the slice map into one slice's
wiring: a bind table (`wire-<slice>.gen.c`) with an `esn_wire_ent` row
per wired symbol, a thunk per forward (`wire-<slice>.gen.s`, a
rip-relative tail jump through the row's slot, `.symver`-bound like the
stub it replaces), and the slice's shim worklist. `wire.c` is the one
bind loop: at load the runtime resolves every export name through a
callback and fills the slots; unresolved rows stay null and are counted.
The mechanism and its alternatives are the bound-table decision record.

## Status

The census (spike 12) is running in the background over the 4855-package
el8 worklist (resumable, logged to `a/build-logs/wp56-wiring-bodies.log`).
When it completes, `census.py report` produces `demand-ranking.tsv`,
`order` cuts the slice queue from it, and the first slice's wiring
begins through `gen-wire.py`. No slices cut yet.
