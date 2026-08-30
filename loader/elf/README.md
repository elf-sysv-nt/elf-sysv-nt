# ELF parsing (WP-31)

The first thing the loader does with an object is read its header, its program
headers, and its dynamic section, and everything the loader does afterward
trusts what that read reported. This package is that read, and it is written on
the assumption that the bytes are hostile. A file that claims to be ELF may be
truncated in the middle of a table, may point a table past its own end, may
size a segment so its file range runs off the image, or may link a version
chain back onto itself. None of that is allowed to fault, loop, or read a byte
outside the image. Each is a rejection with a diagnostic that names the field
at fault.

The parser reads a flat, read-only byte image of the file. It does not map, it
does not relocate, and it does not resolve a symbol; those are WP-32 and the
packages past it. What it produces is a validated view -- a set of file offsets
and counts, every one of them proven to lie inside the image -- so that the
stages which map and relocate never have to re-derive whether an offset is
safe. musl's `ldso/dynlink.c` is the structural model for the walk and was read
as reference only; musl is MIT-licensed and no code was copied, so there is
nothing to carry across but the shape.

## What is parsed

The ELF header, to the extent the format lets a 64-bit little-endian x86-64
object be one: the magic, class, data encoding and version in `e_ident`;
`e_type` restricted to `ET_EXEC` or `ET_DYN`; `e_machine` fixed at
`EM_X86_64`; and the program-header table's location, entry size, and count.
The saturated count `PN_XNUM` is expanded through the first section header the
way the format specifies, rather than rejected.

The program-header table, one entry at a time. Every segment that carries file
content -- `PT_LOAD`, `PT_DYNAMIC`, `PT_INTERP`, `PT_TLS`, `PT_GNU_RELRO`,
`PT_NOTE` -- has its file range checked against the image. `PT_LOAD` segments
additionally must have a nonzero memory size, a file size no larger than their
memory size, and no overlap with one another in virtual space. The segments the
later stages need -- the load list, the interpreter, the TLS template, the
relro range, and the one `PT_DYNAMIC` -- are recorded as checked offsets.

The dynamic section, read from `PT_DYNAMIC` and walked to its `DT_NULL`
terminator. The entries that name a table or a string are resolved: the string
table (`DT_STRTAB`/`DT_STRSZ`), the symbol table (`DT_SYMTAB`/`DT_SYMENT`), the
version-symbol array (`DT_VERSYM`), and the version definition and requirement
tables (`DT_VERDEF`/`DT_VERDEFNUM`, `DT_VERNEED`/`DT_VERNEEDNUM`). A table's
virtual address is translated to a file offset through the `PT_LOAD` list and
the result is bounds-checked; a table whose address is not backed by file bytes
is rejected rather than read. Every name offset that indexes the string table
-- each `DT_NEEDED`, the `DT_SONAME`, and every name in the version records --
is checked against the string table's size.

The version records are walked in full. Each `Elf64_Verdef` and `Elf64_Verneed`
chain is followed for exactly the count the dynamic section declares, and each
auxiliary array for exactly the count its record declares. Every record and
every auxiliary entry must lie in the image, every name offset must lie in the
string table, and every link must advance strictly forward by at least a whole
record. That last rule is what forecloses the looping and overlapping chains: a
`vd_next`, `vn_next`, `vda_next`, or `vna_next` that points back onto the chain
or fails to advance is a rejection, so the walk is bounded and cannot spin.

## Invariants guaranteed to callers

A caller that receives `elf_ok` may rely on all of the following about the
`elf_parsed` it was handed, without re-checking any of it. This is the contract
the rest of the loader is written against.

Every offset in the structure is a file offset into the parsed image, and every
offset paired with a size denotes a range that lies wholly within
`[0, size)`. `phoff` with `phnum` entries of 56 bytes is in range. Each
`load[i]` has `off + filesz` within the image, `filesz <= memsz`, `memsz > 0`,
and a virtual range `[vaddr, vaddr+memsz)` that does not overflow and does not
intersect any other load segment's. When `has_dynamic`, the dynamic array of
`dyn_count` entries plus its terminator lies within the image. When
`has_strtab`, `strtab_off + strsz` is within the image, and every entry of
`needed[0..needed_count)` and, when `has_soname`, `soname`, is strictly less
than `strsz`. When `has_symtab`, the first symbol entry is in range and
`syment` is the 24-byte ELF64 size. When `has_versym`, the first version-symbol
entry is in range. When `has_verdef` or `has_verneed`, the whole declared chain
has been walked and found in range, non-looping, and with every name offset
inside the string table, and the corresponding count is nonzero.

What the contract does not promise is as important as what it does. It does not
promise that a symbol table has any particular number of entries; the count is
not knowable from the dynamic section alone, so only the first entry's presence
is checked, and the stage that learns the count from a hash table checks the
rest against these same bounds. It does not promise semantic correctness -- that
a needed library exists, that a version requirement can be satisfied, that the
entry point is code -- only structural soundness. And it says nothing about a
mapped image; these are file offsets, and turning them into addresses is the
next package's job.

## Threat model

The image is treated as chosen by an adversary who has read the parser. The
defended property is that no input, however malformed, causes a read or write
outside the image buffer, an integer overflow that escapes a bounds check,
unbounded work, or a silent acceptance of a structure that violates the
invariants above. Every rejection is required to name the offending field, both
so a person diagnosing a real object learns what is wrong and so the test suite
can assert that the parser rejected for the reason intended rather than by
luck.

The mechanism is uniform. Nothing in the image is read except through a
bounds-checked accessor that proves its span lies in `[0, size)` before
touching a byte, and reads it with `memcpy` so no misaligned or type-punned
access is performed. Every offset sum that could exceed 64 bits is formed with
an explicit overflow check. Counts that an attacker controls are bounded before
they drive a loop, and every chain walk requires forward progress so a
self-referential link terminates the walk rather than extending it.

Two things are deliberately outside the model. The parser trusts that the
buffer it is given is `size` bytes long and readable; establishing that is the
caller's job, and in the runtime it is the mapping stage that owns it. And the
parser is not a policy engine: it decides whether a structure is well-formed,
not whether a well-formed object should be allowed to run. `AT_SECURE` handling,
interpreter recursion limits, and the like belong to the stages that have the
context to judge them.

## Building and testing

The parser is one translation unit, `elf_parse.c`, with two headers. It depends
only on `stdint.h`, `stddef.h`, `string.h`, and `stdio.h` for `snprintf`, and
carries its own ELF definitions rather than the host's, so it builds host-side
under the pinned RHEL-8.10 emulation toolchain today and is meant to move under
the runtime later with only `snprintf` to reconsider.

`t/` holds the WP-T1 corpus and its drivers. `t/mkfixtures.py` regenerates the
committed fixtures under `t/corpus/`, a well-formed baseline and two dozen ugly
cases, each paired in `manifest.tsv` with the verdict the parser must reach.
`t/unit.c` runs the parser over the corpus and checks each verdict, including
that every rejection names the recorded field. `t/fuzz.c` is the fuzz target: it
mutates the corpus seeds and generates random buffers, and holds the parser to
no crash, no undefined behaviour, a diagnostic on every rejection, and the
invariants above on every acceptance. `t/run.sh` builds and runs all of it and
reports through the session monitor.

Because this host toolchain ships no AddressSanitizer runtime, memory safety is
enforced two ways instead. Every test image is placed so its last byte abuts an
unmapped guard page, which turns a read past the end into an immediate fault
rather than a quiet return of adjacent bytes; and the tests are built with
`-fsanitize=undefined -fsanitize-undefined-trap-on-error`, which needs no
runtime library and turns an overflow, a bad shift, or a misaligned access into
a trap. A clean run is therefore evidence of both properties, not just of the
parser returning an error code. The fuzz count reached on the pinned toolchain
is recorded in `t/run.sh`'s output and in the delivery note in the
implementation plan.
