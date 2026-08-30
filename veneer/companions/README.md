# The companion libraries

WP-54, in progress. The eight shared objects an el8 binary names beside
`libc.so.6`: `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`,
`libcrypt.so.1`, `libresolv.so.2`, `libnsl.so.1`, `libutil.so.1`.

el8 ships these as separate objects rather than the merged glibc of later
releases, so the partition here follows el8's. Each is built by WP-53's
`build-libc`, which was parameterized by soname from the start; this directory
adds the driver that runs it eight times, the reader that checks a binary's
`DT_NEEDED` and verneed against the built tree, and the tests.
