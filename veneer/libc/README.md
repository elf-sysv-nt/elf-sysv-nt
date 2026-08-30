# veneer/libc — synthesized libc.so.6 (WP-53)

WIP. This work package generates the trunk libc.so.6: an ELF shared object
with DT_SONAME=libc.so.6 that reproduces el8's full GLIBC_2.x version
definition ladder (GLIBC_2.2.5 .. GLIBC_2.28, plus GLIBC_PRIVATE), placing every
symbol at the version node el8's own libc.so.6 assigns it (from WP-51's map),
with each symbol forwarding or aliasing into an elfsysv1.dll export per WP-52's
classification. The symbol list is generated from the map plus the classification
by a generator script that emits a version script and .symver directives fed to
the cross toolchain; it is not hand-written. Success is measured against el8's
vendor libc.so.6 (readelf -V ladder match) and el8 dependency generation
(vendor-shaped Provides), reproducing spike 4's base-verdef result.
