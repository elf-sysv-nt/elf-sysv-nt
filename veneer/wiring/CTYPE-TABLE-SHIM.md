# The ctype-table accessors: filling the last stub the cleanest leaf hits

The acceptance harness's first pinned leaf, bzip2, cross-builds to a proper
el8 ELF and imports forty libc symbols. Thirty-nine of them forward or shim.
One is a bucket-4 stub: `__ctype_b_loc`, the accessor glibc's `ctype.h`
character-class macros read through. This note records what that stub is, why
it is filled rather than shimmed, and how the fill is certified.

## What the accessors return

`ctype.h` classifies a character by reading a table, not by branching. Three
accessors front three tables, each a pointer indexed `[-128..255]`:

    __ctype_b_loc()        const unsigned short **   class bits per character
    __ctype_tolower_loc()  const int **              lowercase map
    __ctype_toupper_loc()  const int **              uppercase map

`isalpha(c)` is `(*__ctype_b_loc())[c] & _ISalpha`; `tolower(c)` is
`(*__ctype_tolower_loc())[c]`. The index runs from -128 so that a signed
`char` and `EOF` both land in range without the caller masking.

The class bits are `ctype.h`'s own enum, laid out through `_ISbit`. On a
little-endian x86_64 `_ISbit(n)` is `(n < 8) ? (1<<n)<<8 : (1<<n)>>8`, so the
short is byte-swapped from the natural bit order: `_ISupper` is `0x0100`,
`_ISblank` is `0x0001`. The case maps hold, at index `c`: `EOF` (-1) maps to
-1; the rest of the negative region maps to the unsigned byte `c + 256`
unchanged; 128..255 pass through; only the ASCII letters fold.

## Why this is a filled stub, not a shim

A shim (bucket 3) exists when Cygwin exports a body under the same name whose
ABI differs, and the wiring layer writes a translation over it. Nothing like
that is here. These four names -- the three accessors and the hidden
`__ctype_b` object they front -- are glibc-internal; Cygwin classifies through
newlib's `_ctype_` table, a different layout under a different name, and
exports none of them. WP-52 files them in bucket 4, "absent from the export
surface," correctly. There is no export to translate.

But the answer they return is determinate. In the C and POSIX locales -- the
locale every el8 build tool this harness targets runs in -- the character
classes are fixed by the language, not by Cygwin. So the veneer fills the
stub with the table synthesized from the standard's own definitions, rather
than leaving a body that only reports its own absence. DR (this increment)
records the decision and its limit: a stub is filled only when its body is
determinate data the veneer can produce and certify without a Cygwin call to
stand in front of; a stub whose behaviour needs state the veneer lacks stays
a stub that fails.

The classification is not edited. `classification.tsv` still records these as
bucket 4, because the one fact it holds -- does the export surface carry the
name -- is still no. The filled body lives beside `xlat-core.gen.c` as a
shared wiring component, not in any slice's bind table, because it forwards to
nothing.

## The body, and its certification

`gen-ctype-table.py` emits `ctype-table.gen.c`: the three tables as static
arrays, a `+128`-biased pointer into each, and the three accessors returning
those pointers, each `.symver`-bound to its `GLIBC_2.3` node. The tables are
computed from the standard's class and case rules, not copied from any header.

`t/ctype-table.sh` certifies the body two ways. It regenerates the file and
requires byte-identity with the committed copy, so the table cannot drift from
its generator. Then, where the cross compiler and the pinned el8 image over
WSL are both present, it compiles a dumper against el8's own `ctype.h` -- which
reads glibc's real three tables -- and a probe against this body's accessors,
runs both on the image, and requires the two dumps identical across all 384
entries of all three tables. On 2026-08-31 they were: the synthesized tables
reproduce glibc's real ones byte-for-byte. The behavioural surface is already
covered by `diff/locale/ctype.c`, whose classifier and case-map cases will
resolve through this body on the candidate side once the runtime links it.

## What is left

The scope is the C and POSIX locales; a non-C locale would read this table
where glibc reads a locale-specific one, and closing that seam means
translating Cygwin's per-locale ctype data into glibc's layout at load,
downstream of this static body -- the same line every slice draws for
category-sensitive behaviour.

The acceptance harness now distinguishes the two. `classification.tsv` still
says stub -- the one fact it holds, whether the export surface carries the
name, is still no -- but the wiring layer's filled manifest,
`ctype-filled.tsv`, generated beside this body, names the filled stubs, and
`acceptance/classify.awk` reports a bucket-4 symbol the manifest names as
`filled` rather than `stub`. bzip2's verdict now reads 34 forward, 5 shim,
0 stub, 1 filled: the ctype fill no longer counts against it, and the five
shims are all that stand between it and running.
