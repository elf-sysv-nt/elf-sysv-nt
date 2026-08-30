# The veneer header set

WP-50. The headers are one half of the libc veneer described in ROADMAP §10;
the other halves are the version map (WP-51), the resolution classification
(WP-52), and `libc.so.6` itself (WP-53). This directory holds the header half,
and today it holds the part of that half a compiler bootstrap actually touches:
the feature-test-macro plumbing, certified against el8's glibc 2.28.

It sits this early in the program because WP-14 needs it and because a header
set can be written and certified before anything implements the library behind
it. The exit criterion the plan sets for WP-50 is that a package which probes
the headers and then links the library gets one answer rather than two. Half of
that needs a library to link and is WP-53's; the half that does not is here, and
it is what `t/ftm-diff.sh` certifies.

## What is provided

Under `include/`, four files, and they are the four that everything else in a
glibc header tree expands before anything of its own:

- `features.h` — the feature-test-macro engine. Reports `__GLIBC__` 2 and
  `__GLIBC_MINOR__` 28 (el8's), `__GNU_LIBRARY__` 6, and turns the user's
  `_GNU_SOURCE`, `_POSIX_C_SOURCE`, `_XOPEN_SOURCE` and the rest into the
  `__USE_*` macros the other headers branch on.
- `sys/cdefs.h` — the compiler-facing spellings (`__BEGIN_DECLS`, `__THROW`,
  `__nonnull`, `__REDIRECT`, `__extern_inline`) that a great deal of source
  expands directly. Carried unchanged from the WP-14 bootstrap draft.
- `gnu/stubs.h` — where WP-52's fourth bucket (named interfaces with nothing
  behind them) will surface. Nearly empty today because WP-52 has not run.
- `stdc-predef.h` — the handful of `__STDC_*` facts the compiler preincludes,
  authored to el8's values.

`features.h` is the certified deliverable. The other three are the minimum that
`features.h` transitively includes so that it preprocesses on its own; only
`features.h` is under the diff contract below.

## The reference source and version

The vendor reference is el8's glibc 2.28, taken from the binary package

    glibc-headers-2.28-251.el8_10.40.x86_64

from the Rocky Linux 8.10 vault (DR-0002), whose `usr/include/features.h` has
sha256 `7929e494…d52baf` and whose rpm has sha256 `137c4b4b…57efd5f0e4aab9…`.
Both pins are in `t/ftm-diff.sh`. Per DR-0002 the vendor material is not
vendored into this repository: the test fetches the package at the pinned
version, verifies it by sha256, and caches the single reference `features.h`
under `t/ref-cache/` (gitignored). Offline with no cache and no local rpm, it
prints the pin and skips rather than inventing an answer.

`features.h` here is derived from that vendor header: the feature-test
arithmetic is copied from it verbatim rather than paraphrased, so that it gives
glibc's answer to every combination of input macros and not merely to the ones
someone thought to test. The reasoning and the licence footing for the lift are
in `doc/decisions/0008-veneer-header-provenance.md`.

## The feature-test-macro contract

The certified claim is behavioural, not textual: for the same input feature-test
macros, the veneer's `features.h` resolves the `__USE_*` family — and the source
macros it derives along the way (`_POSIX_C_SOURCE`, `_DEFAULT_SOURCE`,
`_ATFILE_SOURCE`, and so on) — to exactly what the vendor's `features.h`
resolves them to.

`t/ftm-diff.sh` checks this by preprocessing a probe over two include trees that
differ in one file. `sys/cdefs.h`, `gnu/stubs.h` and `stdc-predef.h` are the
veneer's in both trees; only `features.h` is swapped between the veneer's and
the vendor's. Any difference in the reported macros is therefore a difference in
the feature-test arithmetic and nothing else. The probe reports every macro in
the `__USE_*` family plus the identity and the derived source macros; the matrix
covers 43 input sets — the default, strict-ANSI at each C standard, every POSIX
and X/Open level, the ISO C source macros, the large-file and at-file switches,
the reentrancy synonyms, the deprecated BSD/SVID aliases, the fortify levels
under optimisation, C++ at three standards, and several accumulations that
exercise precedence.

Run it:

    bash veneer/t/ftm-diff.sh

It exits 0 when every input set matches, non-zero on any divergence (printing
the diff for each), and 77 when it cannot obtain the reference. The delivery run
reported 43 of 43 matched with `__GLIBC__`=2, `__GLIBC_MINOR__`=28,
`__GNU_LIBRARY__`=6.

## Two macros that are ours rather than glibc's

`features.h` defines `__ELFSYSVNT_HEADERS__`, and `sys/cdefs.h` defines
`__SYSV_ABI__` and `__NO_SYSCALL_INTERFACE__`. The first lets source learn at
preprocess time which platform's headers it is reading, a different question
from the compiler's `__ELFSYSVNT__` and asked at a different time. The other two
say which ABI is above the seam and that there is no kernel under it, so source
that would reach for a syscall can find out at compile time instead of at the
first fault. None of the three affects the feature-test arithmetic, so none
appears in the diff contract.

## What is deferred

- The rest of the C library header surface. `stdio.h`, `string.h`, `unistd.h`
  and the several hundred other headers a real package includes are not here.
  They are later work, and most of them are mechanical once the library they
  declare exists.
- The library side of the exit criterion. "One answer rather than two" needs a
  library to link, which is WP-53. This delivery certifies the header side in
  isolation.
- `gnu/stubs.h` content. It is nearly empty until WP-52 classifies the symbol
  map and generates the fourth bucket into it.
- `__USE_FORTIFY_LEVEL` above what the arithmetic selects. The macro is
  resolved identically to el8, but the fortified entry points it gates are part
  of the library and header surface that is still deferred.
- Wiring the toolchain sysroot to this set. `toolchain/sysroot/include/` holds
  the WP-14 bootstrap draft of these three files, written before there was a
  vendor header to diff against and now superseded by this certified set. That
  draft is left as WP-14 delivered it; regenerating the sysroot from
  `veneer/include/` is a WP-14/WP-15 follow-up (the sysroot build is idempotent
  and reseeded from a template, so it is a one-line source change there, not a
  rewrite). Until then the certified set is `veneer/include/` and the draft is
  understood to be behind it.

## Not verified

- That el8 leaves `features.h` unpatched from upstream glibc 2.28. The reference
  is the installed vendor header as the binary package ships it, which is the
  header a package on el8 actually reads; whether Red Hat patched it away from
  the upstream 2.28 release is not separately checked, and does not matter to
  the contract, which is against what el8 ships.
- Anything about the deferred headers above. Nothing here has been tested
  against source that includes more than `<features.h>` and what it pulls in.
