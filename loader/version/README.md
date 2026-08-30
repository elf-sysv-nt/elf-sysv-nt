# The symbol version matcher (WP-36)

The part of the loader the whole edifice was climbing toward, and the smallest
piece of it. WP-35 built the general resolver and left one seam: an optional
`elf_version_matcher` a caller passes to `elf_object_find` and `elf_lookup`.
This is what plugs into that seam. It reads the three GNU version tables an
object carries — `.gnu.version`, `.gnu.version_d`, `.gnu.version_r` — and answers
the two questions the loader asks of them.

Which definition binds. A reference to `memcpy` that names `GLIBC_2.14` must
reach the body defined at `GLIBC_2.14` and not the `GLIBC_2.2.5` one in the same
library; an unversioned reference reaches the default (`@@`) definition and not
a non-default (`@`) one. `elf_version_match` is the per-candidate callback WP-35
drives, and `elf_version_ctx_init` builds the per-reference context it reads —
the required version name, taken from the reference's own verneed.

Whether every required version is present. A consumer's verneed names, per
dependency, the versions it was linked against. `elf_version_check_needed` walks
that verneed, finds each named dependency among the loaded objects by soname,
and confirms it defines the version. The first absent non-weak requirement
refuses the load with the message a real `ld.so` gives — `version \`NAME' not
found (required by CONSUMER)` — and names the library that should have provided
it; a weak requirement that is absent is tolerated, not refused, exactly as
`ld.so` tolerates it.

## Provenance

Written from the generic ABI and Drepper's account of the version records and
the resolution rule, not from glibc's resolver, which is LGPL and assumes a
Linux kernel this platform does not have (DR-0000, DR-0004). The behaviour it
reproduces is glibc's observable one, and DR-0023 records the load-bearing
reading: an exact version-name match binds; an unversioned reference binds the
default; a versioned reference with no exact match may fall back to the
unversioned base definition but never to a differently-named node; and a
version-definition node carries its predecessors in its verdaux chain, so a
newer node satisfies a requirement for the versions below it.

## Certifying it

    t/run.sh

builds the matcher with the host compiler and runs `version_test.c`, which lays
the version tables out in memory the way an object carries them — a provider
defining `GLIBC_2.2.5` and `GLIBC_2.14` (the latter carrying the former as a
predecessor), and consumers requiring present, weak-absent, and non-weak-absent
versions — and holds the matcher to fifteen checks: the binding rule in both the
versioned and unversioned cases, the default-over-hidden rule, the predecessor
implication, and the three outcomes of the load-refusal check including the
exact refusal string. It is a unit test in WP-35's synthetic style, which the
version tables suit because their layout, not a build, is what the matcher
reads; the real vendor tables it will meet were already shown to parse
identically to `readelf -V` in WP-51.
