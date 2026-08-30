# gcc, second turn — work in progress

WP-15. The bootstrap's second turn: the compiler rebuilt against the veneer's
libc, with C++, threads, shared libraries, and libstdc++. Done when the
compiler builds itself and a C++ exception thrown across a shared library
boundary is caught on the other side.

The WP-50 delivery deferred two things to here: the rest of the C library
header surface, and rewiring the toolchain sysroot to consume
`veneer/include/`. Both land in this package, in that order, because the
stage-2 configure reads the sysroot before it reads anything else.

Order of work:

1. Vendor the full el8 glibc-headers set into `veneer/include/` from the
   pinned reference (`glibc-headers-2.28-251.el8_10.40`, DR-0002), keeping
   `gnu/stubs.h` as the one written exception.
2. Populate the cross sysroot: veneer headers, kernel headers, WP-53's
   `libc.so.6` and `libc.a`, WP-14's startup files.
3. `build-gcc2`: configure against the sysroot, C and C++, shared, posix
   threads; build gcc, libgcc (shared this time), libstdc++.
4. `t/accept2.sh`: the done-when, including the cross-library throw.

This file is replaced by the real README as the pieces land.
