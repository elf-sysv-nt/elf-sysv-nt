# Wiring the bodies, in slices (WP-56)

Work in progress. The forwards become real resolutions into `elfsysv1.dll`
and the shims become translations through WP-55's tables, sliced by
subsystem, with the slice order taken from spike 12's demand ranking.

## Slice machinery

`cut-slices.py map` scans the el8 headers in `veneer/include` with the
cross compiler's `-aux-info`, under `_GNU_SOURCE` so the GNU extensions
and `_LARGEFILE64` names are declared at all, and writes
`symbol-slice.tsv`: every declared function credited to the header that
declares it, and through `slices.tsv` (header -> slice; row order is the
attribution priority) to its slice. The current map covers 3305 symbols
across 101 headers, and `t/real-map.sh` pins what it leaves out: of the
forward map's wired function rows, 275 are unassigned — the underscore
internals no public header declares, `gets`, and the SunRPC `xdr_*`
family, whose headers el8 moved out of glibc into libtirpc.

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

## The differential

`diff-slice.sh <slice>` is the per-slice bar: each case under
`diff/<slice>/*.c` prints observable behaviour, the reference side runs
it on the pinned el8 image over WSL (the WP-T2 environment), the
candidate side runs it through the wired veneer, and the slice passes
when every case prints the same lines on both sides. The compiler,
runner, and reference are injectable; `t/run-tests.sh` uses that to
prove host-only that identical sides pass and a garbled candidate is
reported as a divergence, so the harness is trusted before any slice
is judged by it. The first cases live under `diff/string/`.

## Status

The census (spike 12) is complete: 4855 packages probed, none in error,
2009 distinct glibc bindings demanded. Its products are committed under
`spike/demand-census/results/` — the per-binding demand ranking, the
summary, and the slice order the ranking cuts. The order puts string
first, then the unassigned internals (`__cxa_finalize`,
`__stack_chk_fail` and kin, the most-demanded bindings of all), then
stdio, posix, stdlib, filesystem, on down 26 slices.

The string slice's wiring is generated and committed —
`wire-string.gen.c` / `.gen.s` / `.shims.tsv`, 47 rows wired, one shim
(`__errno_location`) — and `t/real-map.sh` pins it byte-identical to its
inputs. Five diff cases cover the mem*, str*, tokenizing, errno, and
argz/envz families;
writing them caught the crt ending main through `_exit`, which dropped
buffered stdout on redirection, fixed in the startup files by the
main-returns-through-exit decision. The pinned rocky8 image carries no
compiler, so `diff-slice.sh` grew a reference fallback: compile with the
candidate's own compiler, which targets el8's glibc, and run the binary
on the image, where the real ld.so and libc supply the behaviour under
test. Exercised end to end with both sides on el8: five cases, all
match. The errno and argz cases were waiting on `linux/errno.h`; the
el8 kernel headers are laid into the sysroot now
(`toolchain/sysroot/kernel-headers`, taught to unpack with `rpmx.py`
where the root has no cpio). Judging the candidate side awaits the
runtime that loads the wired veneer.
