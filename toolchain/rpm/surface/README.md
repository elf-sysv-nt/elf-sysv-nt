# The rpm surface (WP-62)

This package is confirmation rather than construction. The moment the output
format became ELF, rpm's whole view of a built file repaired itself: `file`
classifies the file, `elf.attr`'s magic pattern admits it to dependency
generation, and `elfdeps` reads its provides and requires with no code of
ours in the path. What lives here is the evidence, produced by real tools
over really built files rather than asserted from the design.

    ./t/run-tests.sh

## What the test proves

`build-libc` produces the veneer's `libc.so.6`, and a consumer referencing
one `GLIBC_2.2.5` function by bare name is linked against it, the way any
fresh el8 link binds. `file` reports both as ELF 64-bit. The magic pattern
read out of el8's own `fileattrs/elf.attr` -- the gate rpm consults before
running `elfdeps` on a file at all -- matches that report. And el8's own
`elfdeps`, the unmodified `rpm-build-4.14.3-32.el8_10` binary running through
WSL, emits `libc.so.6(GLIBC_2.2.5)(64bit)` among the 58 provides of the
built libc and the requires of the consumer, byte for byte what `rpmdeps.py`
predicts. That string in the vendor's exact shape is the WP's done-when.

## rpmdeps.py

The build hosts this project certifies on do not all carry WSL, so the shape
is also held by a reader of our own: `rpmdeps.py` reproduces `elfdeps`' output
line for line -- ordering, the doubled aux lines that give el8's libc 58
provides off 29 nodes, the base-verdef-node naming, the soname filter, the
execute-bit gate on requires, `rtld(GNU_HASH)` -- on top of WP-53's and
WP-54's certified ELF readers. Where the el8 sysroot and WSL are present the
vendor generator judges it directly; where they are not, the recorded shape
stands in and the `deps` check still holds the exact strings.

Three vendor behaviors worth not rediscovering are written into its
docstring: the version provide is named by the base verdef node rather than
`DT_SONAME`; requires are gated on `st_mode`'s execute bits rather than the
ELF type; and a soname that does not contain `.so` and begin `lib`, `ld.`,
`ld-` or `ld64.` is dropped without a diagnostic, which is what would erase a
PE-named face from every generated dependency.

## The rest of the surface

`ldconfig`, the cache, and the search order were WP-33's deliveries
(`loader/graph/`, DR-0011). What WP-62 adds there is the configuration el8
actually ships: `elf-ldconfig -f` now reads `ld.so.conf`'s shape --
directories, comments, and `include ld.so.conf.d/*.conf` with a relative
pattern resolved against the including file -- so the file a package's
scriptlet appends to is read as the vendor wrote it. The stage 0.5 admission
of a PE dependency generator in `doc/symbol-versioning-formats.md` is marked
superseded by this delivery; it belonged to the PE route.

## Not verified

That rpm's file-attribute dispatch runs end to end -- `rpmbuild` calling
`fileattrs` calling `elfdeps` inside a real package build. Spike 4 recorded
the same gap; it needs an rpm build host carrying the generator, which is
WP-63's installation concern and WP-T4's harness. What is measured here is
each gate in that chain answering correctly on its own.

That any vendor tool reads `/etc/ld.so.cache` directly rather than through
`ld.so`. DR-0011 left that question to this record; nothing in the elfdeps
path touches the cache, and no such reader has been found, so the loader's
own cache format stands. A tool that surfaces later gets the glibc-format
writer DR-0011 already scoped, as its own record.

That `--filter-private` filtering of `GLIBC_PRIVATE` matches the vendor's.
el8's macros pass it only when `%__filter_GLIBC_PRIVATE` is defined, no
fixture here carries a private node, and rpmdeps.py does not implement it.
