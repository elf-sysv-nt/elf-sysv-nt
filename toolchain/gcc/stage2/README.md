# gcc, second turn

WP-15. Stage one was `--without-headers`, C only, no threads, no shared: a
compiler that trusts nothing because there was nothing to trust. This turn
configures the same source against the sysroot `toolchain/glibc/build-glibc`
filled -- the ported glibc, its headers and startup files, the kernel uapi
set -- and builds what stage one could not: shared libgcc, posix threads,
C++, and libstdc++.

    ./build-gcc2
    ../t/accept2.sh

`build-gcc2` is resumable on purpose. An unattended runner gets a bounded
slice of time; if the build tree already has a Makefile the configure is
skipped and make continues where it stopped. Run it until it prints
`stage2: done`.

The first bootstrap ran against the veneer, which is retired (DR-0097),
and surfaced one thing worth remembering about the face it presented: el8
binaries carry an undefined `atexit` because glibc keeps it in
`libc_nonshared.a`, not in `libc.so.6`, since it must capture the
registering module's own `__dso_handle`. The real glibc has that shape by
construction.

`t/accept2.sh` measures the done-when as far as it runs today, and says so.
The compiler rebuilt its own runtime from its own source tree, and the
throw/catch pair across a shared library boundary links with every artifact
the unwinder needs -- `PT_GNU_EH_FRAME` in both halves, the `.eh_frame`
tables, `__cxa_throw` imported and answered, `dl_iterate_phdr` exported by
libc. The catch itself runs when the loader can run a binary;
that day, the run becomes the test.
