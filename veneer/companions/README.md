# The companion libraries

WP-54. The eight shared objects an el8 binary names beside `libc.so.6`:
`libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libcrypt.so.1`,
`libresolv.so.2`, `libnsl.so.1`, `libutil.so.1`.

    ./build-companions
    ./t/run-tests.sh

el8 still ships these as separate objects rather than the merged glibc of
later releases, so the partition follows el8's. Nothing here generates
anything new: WP-53's `build-libc` was parameterized by soname from the start,
and `build-companions` runs it once per companion, each into its own build
subdirectory because `build-libc` reseeds its directory on every run. The
companion set is read from `veneer/version-map/libraries.tsv`, the file WP-51
extracted the map from, so a library cannot be in the map without being built
here. Which glibc objects count as companions was DR-0013's open question and
is settled in DR-0032: these eight and no more.

Like libc, the surface is real and the bodies are not. Each library carries
the vendor's soname, node ladder and symbol-to-node assignment, and each
build writes a forward map naming the `elfsysv1.dll` export behind every
symbol.

## The measurement

The exit criterion is that a vendor binary's `DT_NEEDED` list is satisfied
entirely from our tree with no name left over. Running that against a real
el8 binary would want a network, an el8 mirror and a Linux host, so it is
reimplemented the way WP-53 reimplemented `elfdeps`: from the file format.
`t/mk-standin.py` writes a binary that carries what an el8 binary carries — a
`DT_NEEDED` entry and a verneed entry against each of the nine libraries,
with each referenced symbol picked from the library's own forward map rather
than written down to rot — and `elfneeds.py` reads the requirements back out
of the linked file and resolves each against the tree. `elfneeds.py` walks
the section headers itself, reuses `provides.py`'s certified ELF reader, and
reports every `DT_NEEDED` name and every `(library, node)` verneed pair as
satisfied or left over.

`t/run-tests.sh` makes six checks: `build` runs both builders clean;
`surface` compares each companion's export set name by name against the map;
`ladder` diffs each emitted node tree against the vendor's node list;
`needed` is the exit criterion above, nine names and nine verneed pairs, none
left over; `leftover` withholds `libnsl.so.1` from the tree and requires the
checker to say so, since a checker that cannot fail proves nothing; `fuzz`
mutates the stand-in and requires `elfneeds.py` to refuse rather than crash.

One vendor fact worth keeping: `libnsl.so.1` exports nothing at a default
binding — its whole surface is hidden-versioned at `GLIBC_PRIVATE` — so the
stand-in reaches it through an explicit `symbol@version` reference, the form
every el8 binary that needs that library carries.
