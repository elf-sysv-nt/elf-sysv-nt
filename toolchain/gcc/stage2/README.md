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

1. DONE — `veneer/include/vendor-headers` vendors all 417 headers of the
   pinned `glibc-headers-2.28`, `gnu/stubs.h` the one written exception.
2. DONE — the sysroot carries the veneer headers (`build-csu`, rewired),
   el8's kernel uapi set (`../sysroot/kernel-headers`), and the nine veneer
   libraries plus `libc.a` (`../sysroot/install-veneer`). A dynamic hello
   links clean with the stage-1 compiler.
3. IN PROGRESS — `build-gcc2` configures against the sysroot: C and C++,
   shared, posix threads, libstdc++. It is resumable by design; the next
   worker run executes `./build-gcc2` and reruns it until it prints
   "stage2: done".
4. TODO — `t/accept2.sh`: the done-when. The compiler compiles its own
   source, and a C++ exception thrown across a shared library boundary is
   caught on the other side. The second claim needs the loader to run the
   result and may wait on the runtime being far enough along; if it cannot
   run yet, the test links the pair and verifies the unwind tables and
   PT_GNU_EH_FRAME instead, and says plainly which claim it measured.

This file is replaced by the real README as the pieces land.
