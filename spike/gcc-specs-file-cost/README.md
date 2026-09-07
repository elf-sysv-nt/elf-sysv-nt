# What a `specs` file costs the link

DR-0061 makes the granule-separable link a toolchain default by installing
`toolchain/gcc/default.specs` as `$prefix/lib/gcc/$target/$version/specs`,
where gcc reads it at startup. The file appends one option to `*link:` and
looks harmless.

What does the driver stop passing when that file is present?

Two things, and neither is named in the file: `--eh-frame-hdr`, so the linker
writes no `.eh_frame_hdr` section and no `PT_GNU_EH_FRAME` segment; and
`-lgcc_s`, replaced by `-lgcc`, so a link that should reach the shared libgcc
reaches the static archive instead. An **empty** file at the same path costs
exactly the same two, which is the finding: the loss follows the file's
presence, not its contents, so it cannot be fixed by writing the file more
carefully. `results-2026-09-07.txt` is the transcript;
`finding=any-specs-file-drops-eh-frame-hdr-and-shared-libgcc`.

## Why it matters

Both losses are silent and both are load-bearing.

`PT_GNU_EH_FRAME` is how `_Unwind_Find_FDE` locates a frame's unwind tables
through `dl_iterate_phdr` at run time. Without it a C++ exception thrown
across a shared-library boundary finds no tables and terminates, and nothing
about the build says so: the objects compile, the link succeeds, and
`.eh_frame` is present, so every artifact a reader would think to check looks
right. WP-15's exit criterion is exactly that throw and catch.

`-lgcc` for `-lgcc_s` moves the unwinder's implementation from a shared
object into a static archive, which is why `libstdc++.so`'s
`_Unwind_GetTextRelBase@GCC_3.0` and its neighbours went unresolved: the
versioned symbols live in `libgcc_s.so.1` and the archive does not carry
them.

The two records that rest on the mechanism are DR-0061, which chose it, and
DR-0062, which cites "the max-page-size default already there" as precedent
for how a target default is carried. Both are why this is a spike and not a
bug report: the fix is a design question about which layer holds a
target-mandated link default, and it wants a measurement under it.

**Gates.** DR-0061; DR-0062; `toolchain/gcc/default.specs` and its install in
`toolchain/gcc/build-gcc` and `toolchain/gcc/stage2/build-gcc2`;
`toolchain/gcc/t/accept2.sh`'s unwinder claims;
`toolchain/gcc/t/granule-default.sh`.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Needs the cross toolchain on `PATH`; seconds, no build products.

## Method

The probe asks the driver what it would run, with `-###`, and counts three
tokens on the `collect2` line: `--eh-frame-hdr`, `-lgcc_s`, and the
`max-page-size` the specs file exists to add. It does this for five states of
`$prefix/lib/gcc/$target/$version/specs`: absent, empty, `*link:` appending
nothing, `*self_spec:` appending nothing, and the committed
`default.specs` as installed. A C++ shared link is used, because
`-lgcc_s` is the C++ driver's default and the loss is invisible to a C link
that never wanted it.

The file has to move for the duration, since gcc reads it from a fixed path
and `-B` does not displace it: a prefix given with `-B` is searched in
addition, so the installed file still applies and the two states cannot be
told apart. The script moves it aside and restores it under a trap that also
catches `INT` and `TERM`. If a hard kill ever leaves it missing, the file is
a copy of a tracked one and

    install -m 644 toolchain/gcc/default.specs \
        $prefix/lib/gcc/$target/$version/specs

puts it back.

## What this does not reach

One gcc, 13.3.0, built for one target. The mechanism behind the loss is not
identified here — this measures what the driver does, not why — so a
different gcc release may cost a different set. The count is of what the
driver *passes*; that `--eh-frame-hdr` is what produces `PT_GNU_EH_FRAME` is
read from the linked output in `toolchain/gcc/t/accept2.sh`, not here.
