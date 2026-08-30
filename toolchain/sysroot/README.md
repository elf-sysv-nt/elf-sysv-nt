# The sysroot's below-the-libc headers

What lives here changed in WP-15. The three-file header draft WP-14 kept in
`include/` was superseded by the vendored el8 set in `veneer/include/` and has
been deleted; `build-csu` now seeds the sysroot's `usr/include` from the
veneer directly.

What remains here is the layer below glibc's: `kernel-headers` fetches el8's
kernel uapi package at a pinned version (DR-0002), verifies it, and lays its
headers into the sysroot after `build-csu` has reseeded. The order matters
and is stated in both scripts: reseed sweeps, this adds.

    ../csu/build-csu
    ./kernel-headers

The platform has no kernel, so a header set describing one deserves a
sentence. The packages above the floor were compiled against these constants
-- `PATH_MAX` out of `<linux/limits.h>`, ioctl numbers, socket option values
-- and the veneer forwards calls that carry them, so the numbers a package
sees at compile time must be the ones el8's build saw. Nothing here promises
an interface works; `gnu/stubs.h` and WP-52's classification carry that
answer. These files only make the constants match.

## Not verified

That 4.18.0-553 is the right kernel-headers build. el8_10 shipped later
erratum builds; the release build is what the distribution's own glibc was
built against, which is the tightest claim available without measuring, and
no measurement has compared the uapi surface across erratum builds.
