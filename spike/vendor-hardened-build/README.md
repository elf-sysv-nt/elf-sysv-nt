# Whose artifact is the non-PIE bzip2?

WP-56 parked on a placement conflict. bzip2, as the acceptance harness builds
it, is a non-relocatable `ET_EXEC` whose first `PT_LOAD` wants `0x400000`, and
the faced runtime has already mapped something across that address by the time
any loader code runs. Three candidates were scored against that reading, and
each reversed an accepted decision, so the ladder reached tier 8 and stopped.

The scoring rested on an assumption nobody had checked: that the `ET_EXEC` was
el8's shape, and that building bzip2 any other way would cross the DR-0000
refaced floor. This spike checks it.

## What it measures

Three readings, from pinned artifacts rather than from memory.

Red Hat's own binary rpm, unpacked and read with `readelf`. This is the whole
question in one field — if the shipped `/usr/bin/bzip2` is an `ET_EXEC`, the
harness is faithful and the conflict is real; if it is an `ET_DYN`, the
harness is the deviation.

The macro chain that produced it. `bzip2.spec` builds with
`CFLAGS="$RPM_OPT_FLAGS ..."` and `LDFLAGS="%{__global_ldflags}"`; those macros
resolve, through `redhat-rpm-config`, to two `-specs=` arguments; and those two
specs files carry the injections that make RHEL 8 userland position-independent
by default. Each link in that chain is reported as a yes or no read out of the
package, because a chain asserted from documentation is a recollection.

Both cross builds, from one unpacked source tree. `make CC=%CC% bzip2` is
`acceptance/packages.tsv`'s current build line. The second build hands make the
flags el8's macros expand to, with the two `-specs=` arguments replaced by what
those specs files inject, since the specs files belong to Red Hat's rpm
configuration and are not in this sysroot. That replacement is the spike's one
substitution, and it is why the macro-chain findings are here at all: they are
what makes the replacement checkable rather than asserted.

## The finding

`results-2026-09-02.txt`. The vendor ships an `ET_DYN` with zero-based
segments; the naked Makefile produces an `ET_EXEC` at `0x400000`; the same
source under the vendor-effective flags produces an `ET_DYN`, zero-based and
granule-separable under DR-0061. So the image that cannot be placed is the
harness's artifact, and building it the way the vendor does upholds DR-0000
rather than crossing it.

The verdict line is computed from the three `e_type` readings, not written into
the script. Should Red Hat ever have shipped an `ET_EXEC` here, the rerun says
`refuted` and names all three types, and the reframe that rests on this dies
with it.

## Rerunning

    bash measure-vendor-build.sh -D /c/-/el8/vendor-hardened-build

The archives are kept between runs and reused while the pin still matches;
everything else is rebuilt. `-h` prints the options.

Findings are words and reproduce exactly. The header block — compiler,
readelf, mirror, fetch counts — is provenance and moves with the machine.

## Not verified

Whether the flag substitution is behaviourally equivalent to running under
Red Hat's real specs files. It is equivalent for the one property this spike
claims, the image's `e_type` and segment placement, because that property
follows from `-fPIE` and `-pie` alone. `-fstack-clash-protection` and
`-fcf-protection` are dropped or defaulted elsewhere in this toolchain, and
neither bears on placement. The substitution is recorded as S2 in
`doc/substitutions.md`, and what burns it down is an acceptance run against a
package built by real rpm macros.

Whether the naked build's `ET_EXEC` is peculiar to bzip2. It is not peculiar
in kind — any hand-written Makefile that passes no flags gets the toolchain
default — but this spike measures one package, and the share of el8 that
`%undefine _hardened_build` is a separate count nobody has taken.
