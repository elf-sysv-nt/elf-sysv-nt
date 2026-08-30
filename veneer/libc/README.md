# libc.so.6

WP-53. The trunk the veneer hangs from: an ELF shared object carrying el8's
whole `libc.so.6` surface — the soname, the 29-node `GLIBC_2.x` ladder, and all
2329 exported symbols at the node el8's own library assigns each one.

    ./build-libc
    ./t/run-tests.sh

Nothing about the symbol set is written by hand. `generate.py` reads WP-51's
version map and node ladder and WP-52's classification, and writes three files:
an assembly source with one internally-named definition per map row bound to
its node by a `.symver`, a linker version script naming every symbol under its
node, and `libc-forward.tsv`, which pairs each `symbol@node` with the WP-52
bucket and the `elfsysv1.dll` export it is to reach. The assembler and the
linker turn those into the versioned `.dynsym` and the `.gnu.version_d` ladder.
The two-file arrangement is DR-0025; the short version is that a `.symver`
alone does not keep a symbol exported.

`libc.a` comes out of the same pass, carrying the same symbols under their bare
names. An archive has no version table, so only the default binding of each
symbol can be in it, which is what a static link against glibc gets too. The
startup files it links beside are WP-14's, in `toolchain/csu`, and are not
rebuilt here.

## What is real and what is not

The surface is real and the bodies are not. Every function in the library is a
`ret` and every object is eight zero bytes. What el8's tooling reads — `file`,
`readelf`, `elfdeps`, and a linker resolving a `DT_NEEDED` — sees the vendor's
library, and that is the thing the rest of the tree was blocked on. What the
symbols do arrives later, through the forward map, which already names the
target for each of the 1967 symbols WP-52 found something behind and flags the
192 that need a semantic shim.

## The measurement

`t/run-tests.sh` builds the library and makes seven checks against the linked
file rather than against a recorded output.

The exit criterion's first half is `bindings`: `memcpy@GLIBC_2.2.5` and
`memcpy@@GLIBC_2.14` are both in `.dynsym`, at two addresses, and the map still
says which of the two is the default. A library that collapsed them would
satisfy an el8 binary's verneed and call the wrong code.

Its second half is `elfdeps`. Spike 4 put a synthesized library in front of
el8's own dependency generator and recorded, for the full-ladder case,
"identical, 30 lines each" against the vendor. Reproducing that against this
library the same way would want a network, an el8 mirror and a Linux host. The
derivation is short enough to reimplement instead: `provides.py` reads
`DT_SONAME` and the verdef chain out of the file and writes the provides rpm
would write, and the check asserts the same 30 lines, including spike 4's own
`libc.so.6(GLIBC_2.2.5)(64bit)`. It also asserts what spike 4's misnamed-library
case was for: the version provides are written against the base verdef node,
not against `DT_SONAME`, so the two must be the same string. `build-libc`
refuses to finish if they are not.

The other five: `build` runs the generator clean, `surface` compares the whole
export set against the map name by name, `ladder` reads the emitted node tree
back and diffs it against the vendor's node list, `archive` checks `libc.a`
carries bare names and no versioned symbol, and `fuzz` mutates the library and
checks `provides.py` refuses rather than crashes — it is the one reader here
that walks offsets it did not write.

Two of the seven were watched going red before they were trusted. `surface`
caught the version-node identity objects being emitted twice, once by us and
once by the linker, which is DR-0017's distinction showing up as a link error.
`elfdeps` was run against a deliberately wrong expected soname and refused.
