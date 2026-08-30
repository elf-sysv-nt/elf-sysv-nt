# gcc, second turn

WP-15. Stage one was `--without-headers`, C only, no threads, no shared: a
compiler that trusts nothing because there was nothing to trust. This turn
configures the same source against the sysroot the veneer filled -- the
vendored el8 glibc headers, the kernel uapi set, WP-14's startup files,
WP-53/54's libraries -- and builds what stage one could not: shared libgcc,
posix threads, C++, and libstdc++.

    ./build-gcc2
    ../t/accept2.sh

`build-gcc2` is resumable on purpose. An unattended runner gets a bounded
slice of time; if the build tree already has a Makefile the configure is
skipped and make continues where it stopped. Run it until it prints
`stage2: done`.

The bootstrap surfaced one veneer defect worth remembering: el8 binaries
carry an undefined `atexit` because glibc keeps it in `libc_nonshared.a`,
not in `libc.so.6` -- it must capture the registering module's own
`__dso_handle`. The fix lives where the cause does: `veneer/libc/nonshared.c`
and the `libc.so` GROUP script that `install-veneer` writes.

`t/accept2.sh` measures the done-when as far as it runs today, and says so.
The compiler rebuilt its own runtime from its own source tree, and the
throw/catch pair across a shared library boundary links with every artifact
the unwinder needs -- `PT_GNU_EH_FRAME` in both halves, the `.eh_frame`
tables, `__cxa_throw` imported and answered, `dl_iterate_phdr` exported by
the veneer libc. The catch itself runs when the loader can run a binary;
that day, the run becomes the test.
