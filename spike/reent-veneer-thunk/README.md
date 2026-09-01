# reent-veneer-thunk -- the link-time shape of a runtime-resolving body

`spike/reent-veneer-body` measured why item 2 of `acceptance/reent/README.md`
cannot be a link-time forward: a naive `jmp strtol@PLT` under the veneer's own
`.symver` binds the unversioned reference to the default-versioned definition
the veneer itself provides, so the body jumps to itself. The PE export lives in
`elfsysv1.dll`'s export directory, not the ELF dynamic-symbol table; the WP-27
crossing resolves it at RUN time from `AT_BASE`
(`runtime/face/t/elfcall.c`'s `pe_export` walk). So a real body must name its
target as DATA and resolve it at run time, not name it as an ELF symbol. This
spike measures the link-time shape of that thunk -- the codegen contract
`veneer/libc/generate.py` must meet when it replaces the `ret` stubs.

## The findings, reproduced

`measure.sh` (run 2026-09-01) emits one such thunk for a reent-consuming symbol
(`strtol`) -- a versioned body that hands the name `"strtol"` to a hidden
resolver modelled on `elfcall.c`'s PE-export walk and tail-calls the result --
assembles it, links a versioned ET_DYN, and reads the object back. Four facts
reproduce on this tree, needing only the cross toolchain (no face DLL, no run):

  - `thunk_defines_versioned_symbol=yes`. `strtol@@GLIBC_2.2.5` is DEFINED in
    `.dynsym` as a FUNC with a real body (40 bytes), not the single-byte `ret`
    the veneer emits today (`reent-veneer-runtime`).

  - `thunk_no_elf_self_import=yes`. The object carries NO undefined `.dynsym`
    entry for `strtol` and NO relocation naming it. Unlike the naive forward,
    which held `strtol` DEFINED with the reference self-bound, this body depends
    on no ELF symbol named `strtol` at all -- there is nothing for the linker to
    bind to itself.

  - `thunk_keys_on_export_name=yes`. The literal `"strtol"` is present in
    `.rodata` -- the name the run-time resolver hands the PE export directory.
    The body reaches the face by name, the crossing ABI, rather than by an ELF
    reference the ELF world cannot express.

  - `resolver_stays_private=yes`. The resolver the thunk calls is absent from
    `.dynsym` (hidden visibility): one veneer carries a single private resolver
    that cannot collide with any faced symbol.

## What this settles, and what it does not

Together the four facts fix the codegen contract for item 2: each body is a
versioned definition whose target is a `.rodata` name resolved at run time
through one hidden per-veneer resolver, with no ELF dependency on the faced
name. That is the shape `generate.py` must emit in place of the `ret` stub.

It does not exercise the resolution. The `AT_BASE` walk and `pe_export` here are
a faithful sketch built to link, not to run: whether a thunk so shaped actually
reaches the face export and returns `LONG_MAX`/`ERANGE` across the loader is
item 3 of `acceptance/reent/README.md`, which needs the built `elfsysv1.dll`
face and a reent-consuming ELF specimen entered through the crossing, and stays
deferred behind this and the WP-53 veneer. This spike pins the link-time half
so that codegen rests on a reproduced fact rather than a plan.

## Running it

    bash measure.sh                 # writes the transcript to stdout
    bash measure.sh -o results.txt  # or to a file

It SKIPs (verdict yes) when the cross toolchain is absent, that being an
uncommitted build product. Registered in `test/spike-regen.tsv`, so a rerun that
no longer reproduces fails the way a broken unit test does.
