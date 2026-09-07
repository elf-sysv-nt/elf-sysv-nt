# DR-0108 — the installed specs file is generated from the driver's own specs, never hand-written

Status: provisional
Date: 2026-09-07
Deciding: the build worker, on the finding of spike 53; the operator ratifies
Proposal: none; taken when six of `toolchain/gcc/t/accept2.sh`'s C++ claims went
red and the cause was DR-0061's mechanism rather than anything the sysroot did
Amends: doc/design/target-definition.md § The PIE default
Partially supersedes: DR-0061, on how the granule default is installed; the
requirement DR-0061 states, and the three layers that hold it, stand unchanged

## What was decided

A `specs` file installed at `$prefix/lib/gcc/$target/$version/specs` is
generated at install time from `$target-gcc -dumpspecs`, taken with any
previously installed file moved aside, with the target's additions merged into
the named stanzas. It is never the hand-written fragment alone. The generator
is `toolchain/gcc/install-specs`, both bootstrap turns call it, and it refuses
to leave a file behind whose `-###` link line has lost a token the same driver
passed without it.

## Why a fragment cannot be installed on its own

gcc reads that path at startup, and reading it is exclusive. In `gcc.cc`'s
`main`, at gcc 13.3.0:

    specs_file = find_a_file (&startfile_prefixes, "specs", R_OK, true);
    /* Read the specs file unless it is a default one.  */
    if (specs_file != 0 && strcmp (specs_file, "specs"))
      read_specs (specs_file, true, false);
    else
      init_spec ();

`init_spec ()` is the branch not taken. It is where `LINK_EH_SPEC` is prepended
to `*link`, so `--eh-frame-hdr` reaches the linker, and where the built-in
`-lgcc` is rewritten into the `-lgcc_s` and `-lgcc_eh` selection that a shared
link needs. An installed file replaces that work rather than adding to it, so
the driver runs on the compile-time table plus whatever the file says, and the
two options vanish without being named anywhere.

Spike 53 measured the loss before the mechanism was known, in five states of
the file: absent, empty, `*link:` appending nothing, `*self_spec:` appending
nothing, and the committed fragment. All four present states cost the same two
options, which is why the fix cannot be a more careful fragment. The cost is
not academic. `PT_GNU_EH_FRAME` is how `_Unwind_Find_FDE` reaches a frame's
tables through `dl_iterate_phdr`, so without it a throw across a shared-library
boundary terminates on a system where the build looked clean; `-lgcc` for
`-lgcc_s` is why `libstdc++.so` went unresolved against
`_Unwind_GetTextRelBase@GCC_3.0`.

Dumping the specs and editing the dump is the whole repair. What `-dumpspecs`
prints is the post-`init_spec` table, both losses included, so a file derived
from it carries them into the exclusive read. The generated file is a build
product of one gcc install and belongs to it: paths and the version are baked
in, which is the second reason to generate rather than commit, since the
2026-09-06 regression was a remake that left a stale file behind.

## Consequences

`toolchain/gcc/default.specs` stops being installed and becomes the fragment
the generator merges, which keeps spike 53's committed-fragment state intact
and reproducible. `build-gcc` and `build-gcc2` both call `install-specs` at the
point they used to copy, so the two stay in step by construction rather than by
a comment asking them to. `granule-default.sh` grows the two claims it could
not have made: a test written to confirm one option is present never notices
two others going missing, and it is the missing ones that are load-bearing.

The generator verifies before it commits to the file. A driver whose `-###`
line drops a token it passed without the file is a failure, not a warning, and
the installed file goes back to what it was.

Nothing here rebuilds a toolchain. The repair is an install step, minutes, and
`accept2.sh` is the measurement that it worked.

## What it does not decide

Which layer carries a target-mandated link default. That question is real and
it survives this record: the compiler through `LINK_SPEC` in
`gcc/config/i386/elfsysvnt.h`, or the linker through `ELF_MAXPAGESIZE` for this
target's vector, where the 4 KB default lives at `bfd/elf64-x86-64.c:5625` in
binutils 2.42. The linker holds for a hand-written Makefile that calls `ld`
itself, which DR-0061's own worked example, bzip2, is; the compiler reaches
only what the driver runs. It parks at tier 8 with both survivors named,
because DR-0062 argues from max-page-size being a compiler default and moving
it would dissolve that premise rather than contradict its conclusion. This
record leaves the default where DR-0062 reads it.

Whether the fragment should carry more than the one option. DR-0062 sends
`-fcf-protection=none` to the same place, and the generator merges by stanza
name, so that addition needs no change here.

The tier that discriminated for what is decided above is 1. A driver that
silently stops passing `--eh-frame-hdr` is not a preference between mechanisms.

## Not verified

That the two options were the whole of the cost when the spike ran. Spike 53
counts three tokens on the `collect2` line rather than diffing it, so a third
difference neither token names would not have appeared, and the transcript
cannot now be read for one. From here on the question is closed by
construction: `install-specs` compares the whole token set, scratch file names
aside, and a token that was there before and is not there after is a failure.

That the generator behaves the same against the cross toolchain. It was
exercised end to end, but against a stand-in: a Linux gcc 11 reached through a
`-B` prefix, which puts the file on the same search the installed path is on.
There the generated file kept all three options, the hand-written fragment lost
two, and a rerun over the fragment recovered them. The cross compiler is
Cygwin's, and the first real run of this is the next `build-gcc2`.

That the C++ unwinder works once `PT_GNU_EH_FRAME` returns. Every claim in
`accept2.sh` is a link-time artifact by that file's own admission, and the
throw crossing a DSO has never executed on this platform. What is asserted is
that the tables will be findable.

That gcc's exclusive read is the same in another release. It is read from
13.3.0, which is what `gcc.pin` names.
