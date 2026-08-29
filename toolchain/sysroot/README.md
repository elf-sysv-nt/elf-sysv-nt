# The header set

WP-50, pulled forward out of phase 5 because WP-14 needs it. A header set can
be written before anything implements it, which is the whole reason it sits
this early in a plan that otherwise builds leaf to trunk.

Three files, and they are the three that everything else includes:
`features.h`, `sys/cdefs.h`, `gnu/stubs.h`. That is not the glibc header
surface and does not pretend to be. It is the part a compiler bootstrap
touches and the part every other header would expand, so it is where the
feature-test behaviour has to be right before there is anything to be right
about.

## What they claim

`__GLIBC__` 2 and `__GLIBC_MINOR__` 28, which is el8's. That pair is a claim
about the interface a package may name, not about what stands behind any of
it. WP-52 sorts every symbol into four buckets and the fourth is symbols with
nothing behind them at all; `gnu/stubs.h` is where that bucket surfaces, and
it is nearly empty today because WP-52 has not run rather than because the
answer is nothing.

The feature-test arithmetic follows glibc to the letter. `_GNU_SOURCE` implies
the lot, an absent macro yields `_DEFAULT_SOURCE` rather than nothing, and the
`__USE_*` results are what the rest of a header set tests. Resolving these
differently would compile the same source into a different program, which is
the failure the exit criterion is written against: a package that probes the
headers and then links the library has to get one answer, not two.

Two macros are ours rather than glibc's. `__SYSV_ABI__` says which ABI is
above the seam, and `__NO_SYSCALL_INTERFACE__` says there is no kernel under
it, so source that would have reached for a syscall or a vDSO can find out at
compile time instead of at the first fault. Both are in `sys/cdefs.h` beside
`__ELF__`, which is true here for the first time and is the reason the project
exists.

`__ELFSYSVNT_HEADERS__` is not the same question as `__ELFSYSVNT__`. The
compiler defines the latter, and `config.guess` asks it, at a moment when no
header has been read. This one is for source that wants to know which headers
it is reading.

## Where the exit criterion actually stands

Not met, and it cannot be met yet. "A package that probes the headers and then
links the library gets one answer rather than two" needs a library to link,
and that is WP-53. What can be checked today is the half that does not need
one: the headers preprocess, the feature-test macros resolve as glibc's do,
and a freestanding compile against them works. `t/` under `toolchain/gcc`
covers that much.

## Not verified

The other several hundred headers. `stdio.h`, `string.h`, `unistd.h` and the
rest of the surface a real package includes are absent, so nothing here has
been tested against source that was not written for it.

That the feature-test arithmetic matches glibc 2.28 exactly. It was written
against glibc's documented behaviour and reads correctly, and nobody has
diffed the `__USE_*` results against a real 2.28 `features.h` over a matrix of
input macros. That diff is cheap, the vendor headers are one `glibc-headers`
package away, and it is the obvious next thing.

That `__NO_SYSCALL_INTERFACE__` is a name anyone will look for. It is invented
here; no package tests it today, and its value is to whoever ports one.
