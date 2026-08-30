# The rpm surface (WP-62)

Work in progress. This package is confirmation rather than construction: the
format change to ELF is what repairs rpm's view of a built file, and what
lives here is the evidence that it did. Three gates are checked against real
tools rather than asserted -- `file` reporting ELF, rpm's `elf.attr` magic
pattern matching that report, and el8's own `elfdeps` emitting
`libc.so.6(GLIBC_2.2.5)(64bit)` off our built objects in the vendor's exact
shape. The search-path configuration `ldconfig` reads on el8 -- `ld.so.conf`
and its `include` directive -- lands in `loader/graph/ldconfig.c` alongside.
