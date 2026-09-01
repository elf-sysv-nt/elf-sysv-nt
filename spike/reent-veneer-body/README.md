# reent-veneer-body -- what a real forwarding body must reach, and how

The `reent-tls-bringup` rung of `acceptance/to-green.tsv` (WP-56's road to
green) needs the WP-53 `libc.so.6` veneer's bodies to reach `elfsysv1.dll`,
where the WP-27 face brings the reent up. `spike/reent-veneer-runtime` measured
that today every FUNC/IFUNC body is a single-byte `ret`; item 2 of
`acceptance/reent/README.md` is generating the forwarding bodies that replace
those stubs. This spike measures two facts about that codegen's target surface,
so item 2 rests on reproduced facts rather than a plan -- the companion to
`reent-stub-link` (item 1) and `reent-veneer-runtime` (the stub bodies).

## The findings, reproduced

`measure.sh` (run 2026-09-01) builds the veneer for a fresh `libc-forward.tsv`
and reads the built face DLL, and both findings reproduce on this tree:

  - `targets_all_exported=yes`. Every forwarding target the map names -- its
    `forward-same` and `forward-alias` rows, 1047 distinct names -- is a real
    export of the built `elfsysv1.dll`. So the map's `target` column is a sound
    destination: a forwarding body has somewhere real to reach.

  - `link_forward_self_references=yes`. A naive link-time forwarding body does
    NOT reach that export. Emitting, for one reent-consuming symbol (`strtol`),
    a `jmp strtol@PLT` under the veneer's own `.symver v_strtol, strtol@@GLIBC_2.2.5`
    and linking a versioned ET_DYN leaves `strtol@@GLIBC_2.2.5` DEFINED in
    `.dynsym` with no undefined import of `strtol`: the linker bound the
    unversioned reference to the default-versioned definition the veneer itself
    provides, so the jump resolves to itself.

## Why this shapes item 2

The PE export `strtol` lives in `elfsysv1.dll`'s export directory, which is not
an ELF dynamic symbol. The WP-27 crossing resolves it at RUN time: the specimen
walks the auxv to `AT_BASE`, finds the export in the PE export directory, and
calls it System V (`runtime/face/t/elfcall.sh`). A link-time forward cannot name
that; it can only name an ELF symbol, and the only ELF `strtol` in scope is the
veneer's own definition. So item 2 is generating runtime-resolving thunks
against the WP-27 crossing ABI -- each body finding its target in the face at
run time -- not a link flag and not a `.din`-style alias. That, `reent-stub-link`'s
item 1, and a reent-consuming ELF body across the crossing (item 3) are what
stand between the veneer surface and the `reent-tls-bringup` signal.

## Running it

    bash measure.sh                 # writes the transcript to stdout
    bash measure.sh -o results.txt  # or to a file

It SKIPs (verdict yes) when the cross toolchain or the built `elfsysv1.dll` face
is absent, both being uncommitted build products. Registered in
`test/spike-regen.tsv`, so a rerun that no longer reproduces fails the way a
broken unit test does.
