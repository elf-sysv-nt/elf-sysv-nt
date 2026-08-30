# The version map (WP-51)

Every symbol el8's glibc exports, bound to the version node el8's own glibc put
it at, for `libc.so.6` and the eight companion libraries a vendor binary links.
It is extracted from the vendor binaries rather than written down, because
several thousand symbol-to-node bindings maintained by hand would be wrong
within a month. The extractor, the pins it reads, and the map it produces are
all here; the reproduce test reruns the extraction and diffs it against the
committed map, and cross-checks that map against `readelf`.

This is the second quarter of the libc veneer described in ROADMAP §10. WP-50
delivered the headers; this delivers the map WP-52 classifies and WP-53 turns
into a real `libc.so.6`.

## Source of record

The vendor material is el8's glibc 2.28 and its version-mates, taken from the
Rocky Linux 8.10 vault (DR-0002) and, per that record, not vendored into this
repository. The extraction fetches the pinned binary packages, verifies each by
sha256, unpacks them outside the tree, and reads the libraries out. Three
packages carry the nine libraries:

    package                                  sha256 (rpm)
    glibc-2.28-251.el8_10.40.x86_64          f9ee0ed8…f01bbdf
    libnsl-2.28-251.el8_10.40.x86_64         90780f74…56758e9
    libxcrypt-4.1.1-6.el8.x86_64             da47347f…651f1a468

`glibc` carries `libc.so.6` and seven of the eight companions. Two do not come
from glibc, and DR-0013 records why the companion set spans three packages
rather than one: el8 built glibc `--disable-crypt`, so `libcrypt.so.1` is
`libxcrypt` and carries an `XCRYPT_2.0` node beside its GLIBC ones; and
`libnsl.so.1` ships in its own package at glibc's own build. `packages.tsv` is
the authoritative pin and `libraries.tsv` names the nine files and the soname
each must report.

## The files

`extract-version-map.py` parses a library field by field — section headers,
`.dynsym`, `.gnu.version`, `.gnu.version_d`, `.dynamic` — and writes the map. It
calls no `readelf`: the map is its own reading of the vendor file, so the test's
`readelf` cross-check is a second parser rather than a restatement of the first.
`fetch-vendor.sh` fetches and unpacks the pinned packages, reseeding its output
each run; `rpmx.py` is the rpm payload reader it uses, carried from the
`versioned-libc` spike so this artifact stands on its own. `packages.tsv` and
`libraries.tsv` are the pins and the manifest. `glibc-version-map.tsv` and
`glibc-version-nodes.tsv` are the committed generated artifacts. `t/reproduce.sh`
is the certification.

## The format

`glibc-version-map.tsv`, tab-separated, no header, sorted by library (in the
manifest's order), then symbol, version, binding and type:

    soname   symbol   version   binding          type                       bind
                                 default|hidden   func|object|tls|ifunc|…    global|weak|local

`version` is the `.gnu.version_d` node the symbol is bound to, and `-` for a
defined symbol that carries no version. `binding` is `default` for the `@@`
form — the definition a fresh link picks — and `hidden` for the `@` form, an
older definition kept so old callers still resolve. So `memcpy` appears twice:

    libc.so.6   memcpy   GLIBC_2.14   default   ifunc   global
    libc.so.6   memcpy   GLIBC_2.2.5  hidden    func    global

which is exactly `memcpy@@GLIBC_2.14` and `memcpy@GLIBC_2.2.5` as
`readelf --dyn-syms` prints them. Only defined symbols are recorded; an
undefined import belongs to a `.gnu.version_r` requirement rather than to what
the library provides, and this map is the provides side.

`glibc-version-nodes.tsv` is the version-node ladders, one row per node:

    soname   index   node   flags          parents
                            base|none|weak  nearest-first,comma-joined | -

`index` is the `vd_ndx` the versym table points at; `parents` is the node's
parent chain, which is why a consumer asking for `GLIBC_2.14` is satisfied by a
library defining `GLIBC_2.28` — the newer node descends from the older. The
`base` node is the library's own identity node, and it is the one spike 4's
trap is about.

## The counts

    library           map rows   nodes (incl. base)
    libc.so.6             2358    30
    libm.so.6            1078    11
    libpthread.so.0       273    12
    libdl.so.2             14     5
    librt.so.1             47     6
    libcrypt.so.1          16     3
    libresolv.so.2        102     5
    libnsl.so.1           129     3
    libutil.so.1            7     2
    total                4024    77

Of the 4024 bindings, 3591 are default and 433 hidden; 134 are ifuncs.
`libc.so.6`'s 30 nodes are the base plus the 29-node ladder `GLIBC_2.2.5`
through `GLIBC_2.28` and `GLIBC_PRIVATE` — the historical gaps at 2.19, 2.20 and
2.21 are real, since x86_64 glibc added no symbols at those versions.
`extract-version-map.py --tree DIR --counts` prints these to stderr.

## The trap

A versioned provide is read off the base `.gnu.version_d` node, not off
`DT_SONAME`; spike 4 measured a synthesized library whose two disagreed and
produced provides that satisfied no package and raised no error. The extractor
enforces the invariant the vendor libraries all hold: the base node's name must
equal `DT_SONAME`, or the extraction fails on that library rather than emit a
map WP-53 would build a broken object from. All nine libraries pass; the check
is here so a hand-edited or mis-synthesized input in some later reuse cannot.

## How WP-52 and WP-53 consume it

WP-52 reads `glibc-version-map.tsv` and sorts every row into one of its four
buckets — forwards to a runtime export under another name, forwards under the
same name, needs a shim, or is a stub with nothing behind it — so the map is the
domain WP-52 partitions and must cover with no symbol left unclassified. WP-53
reads both files: the node ladder to reconstruct each library's
`.gnu.version_d` (base node named for the soname, the 29-node chain with its
parents), and the map to emit the versioned aliases, so that
`memcpy@GLIBC_2.2.5` and `memcpy@@GLIBC_2.14` both exist in the object and bind
independently. WP-54 takes the companion rows the same way. Because the map is
extracted rather than authored, the aliases WP-53 emits are the vendor's aliases
and rpm's `elfdeps` reads the vendor's `Provides` back off the result — which is
the whole point spike 4 measured.

## Regenerating and testing

    ./fetch-vendor.sh --dest DIR                       # fetch + unpack the pins
    ./extract-version-map.py --tree DIR \
        --map glibc-version-map.tsv \
        --nodes glibc-version-nodes.tsv                # regenerate
    bash t/reproduce.sh                                # certify

`t/reproduce.sh` fetches (or reuses a cache), reruns the extraction, diffs it
against the committed files, asserts the 29-node ladder against `readelf -V`,
checks `memcpy`'s two bindings, and confirms every defined dynamic symbol in
`libc.so.6` matches `readelf --dyn-syms` line for line. It exits 0 on success,
1 on any difference, and 77 when it cannot obtain the vendor binaries offline,
the way WP-50's header diff skips. The delivery run reported 4024 map rows and
77 node rows reproducing, the ladder equal to `readelf -V`, and all 2358 of
`libc.so.6`'s defined dynsyms identical to `readelf --dyn-syms`.

## Not verified

- The map is what the vendor binaries carry, read two ways that agree. Whether
  every one of those symbols can be *implemented* behind the veneer is WP-52's
  question and this says nothing about it; it records the metadata, not the
  content.
- `libcrypt.so.1`'s `XCRYPT_2.0` node is carried faithfully because the vendor
  defines it, but whether the veneer ultimately provides that node or only its
  GLIBC ones is a WP-53/WP-54 scope call, noted in DR-0013 and not settled here.
- Only the nine libraries a vendor binary's `DT_NEEDED` reaches are mapped. The
  other objects glibc ships (`ld-linux`, the `libnss_*` modules, `libmvec`,
  `libanl`) are out of the companion set WP-54 defines and are not extracted.
