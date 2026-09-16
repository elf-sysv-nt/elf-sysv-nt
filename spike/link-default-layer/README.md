# Which layer can carry a target-mandated link default

DR-0061 requires every image the platform loads to be linked granule-separable,
and says the toolchain carries the default so that a package is granule-separable
"whether or not its own build system knows to ask". The mechanism carrying it
today is a specs file the gcc driver reads. DR-0108 repaired that mechanism and
deliberately did not move it, because where the default belongs is the
operator's call and the ladder parks it at tier 8.

Two candidates survive:

- **A, the compiler.** `-z max-page-size=0x10000` joins `LINK_SPEC` in
  `gcc/config/i386/elfsysvnt.h`, beside the subtarget default that header
  already mandates. Its own rationale is that it "carries what the target
  mandates rather than what a caller suggests". Costs a gcc rebuild, hours.
- **D, the linker.** `ELF_MAXPAGESIZE` for this target's vector in bfd, which
  is `0x1000` and is the reason the default is 4 KB. Costs a binutils rebuild,
  minutes, and reaches a link the gcc driver never ran.

The argument for D is that DR-0061's own worked example is bzip2, whose
hand-written Makefile passes no max-page-size. The argument is about reach, and
reach is a fact about the toolchain rather than a preference, so it is measured
here rather than argued in the record that reads it.

## Running it

    ./measure.sh -o results-$(date +%F).txt                    # baseline only
    ./measure.sh -a /c/-/x-a -d /c/-/x-d -o results-$(date +%F).txt

Candidate D is a binutils and nothing else, because raising a linker's default
does not need a compiler rebuilt to measure it. A prefix holding an `ld` and no
`gcc` is driven by the baseline compiler, and the `readelf` that reads the
result comes from the same prefix as the `ld` that wrote it.

Reaching that second linker takes more than `-B$prefix/bin/`. The driver looks
the linker up under the bare name `ld`, and a toolchain prefix holds it as
`$target-ld`, so a `-B` at that directory contributes no candidate `ld` at all
and the driver silently falls through to the baseline's. The script builds a
shim directory holding the candidate's linker under the name the driver asks
for, and then asserts `-print-prog-name=ld` names it, reporting `shim-failed`
rather than a verdict if it does not.

A prefix that has not been built is reported as `not-built` rather than failing,
because two of these three do not exist until somebody spends the rebuild. The
baseline alone is worth a transcript: it records what the mechanism in force
does and does not reach.

## Method

Five probes per prefix, each a verdict rather than a number.

`driver` asks the gcc driver, with `-###`, whether `max-page-size=0x10000` is on
the link line at all. `direct-ld` compiles an object with the driver and then
links it with `ld` itself, which is the shape a hand-written Makefile has and
the one only a linker-side default reaches. `no-specs` moves the installed specs
file aside and links an image, since a candidate that needs the specs file has
not replaced it; the file is restored immediately, and `-B` cannot stand in for
moving it, because a `-B` prefix is searched in addition to the installed path
rather than instead of it. `override` links with `-Wl,-z,max-page-size=0x1000`
and checks that the smaller page still wins, because DR-0008's own test builds a
sub-granule image on purpose and a default that could not be overridden would
disarm the test of the layer that is the real guarantee. `unwinder` restates
DR-0108's property: `--eh-frame-hdr` and `-lgcc_s` are still passed.

Every probe that reads an image reads **separation between consecutive
`PT_LOAD` addresses**, not `p_align`. The two are different quantities and the
difference is not cosmetic: `p_align` comes from `ELF_COMMONPAGESIZE`, which
neither candidate touches, while max-page-size moves where the linker places
the next segment. A raised linker default therefore shows as `0x10000` between
segments with `p_align` still reading `0x1000`. Separation is also the quantity
DR-0008 is written in terms of — it refuses two segments of unlike protection
that *share a granule* — so it is both the correct instrument and the one the
record already speaks.

A candidate carries the default when `no-specs` holds and `override` is honored.
Whether `direct-ld` is also granule-aligned is what separates the two layers,
and it is the whole of the bzip2 argument.

## What candidate D said

D was built on 2026-09-16: binutils 2.42 with `ELF_MAXPAGESIZE` raised from
`0x1000` to `0x10000` at `bfd/elf64-x86-64.c:5625`, which is the definition
that precedes `x86_64_elf64_vec`, installed into its own prefix and driven by
the baseline compiler.

**D carries the default, including for a direct `ld` invocation.**
`direct-ld=granule-aligned` and `no-specs=default-holds`, against the
baseline's `sub-granule` and `default-lost`, with `override` still `honored`
and both unwinder properties still passed. That is the strongest verdict this
spike defines, and it is DR-0061's bzip2 argument satisfied: a link that never
runs the driver comes out granule-separable.

The chain is now traced end to end rather than assumed. `ld/ldemul.c:243` sets
`link_info.maxpagesize` from `bfd_emul_get_maxpagesize (default_target)`, which
returns `xvec_get_elf_backend_data (target)->maxpagesize`, which `elfxx-target.h`
fills from `ELF_MAXPAGESIZE`. Line 5625 is the right lever and always was. The
assignment is guarded by `if (link_info.maxpagesize == 0)`, so an explicit
`-z max-page-size` still wins, which is why `override` stays honored.

### Why the first run said it changed nothing

Three defects in this script, not one, and all three are the same mistake:
the instrument was never checked against a case whose answer was known.

1. **The verdict read `p_align`.** `p_align` comes from `ELF_COMMONPAGESIZE`
   (`elf64-x86-64.c:5626`), which is `0x1000` and which D does not touch. D's
   images separate segments at `0x10000` with `p_align` still `0x1000`. A
   probe reading `p_align` cannot see a linker-side default however large.
2. **`no-specs` grepped the driver's command line.** A compiler-side default
   appears there; a linker-side one never does. The probe reported every
   linker candidate as having lost a default it was still applying.
3. **`-B$prefix/bin/` never displaced the linker.** The driver resolves the
   bare name `ld`, the prefix holds `$target-ld`, and the driver fell through
   to the baseline's copy in `$baseline/$target/bin/ld`. Confirmed with
   `-print-prog-name=ld`, which names the baseline under `-B` and the
   candidate only when asked for `$target-ld`. So the driver-mediated probes
   were measuring the baseline linker under the candidate's name.

The first two made a working candidate look inert. The third meant two of the
probes were not exercising the candidate at all. Each is now fixed in
`measure.sh` with the reasoning at the site, and the shim is asserted rather
than assumed so that the third cannot recur silently.

The general lesson, which is `verify-first`'s and is worth stating in the file
that learned it: a probe that has never produced a *different* answer for a
case known to differ is not yet an instrument. The baseline and D differ by
construction; nothing checked that the script could tell them apart.

### Where the default actually lives today

Not in the linker. The shipping prefix's `ld` is stock: a bare `ld` link
separates at `0x1000`. The granule default in force is the gcc driver's, from
`toolchain/gcc/default.specs` —

    *link:
    + -z max-page-size=0x10000

— appended to `*link` and merged into the installed specs file by
`install-specs`. That is the mechanism DR-0108 repaired, and DR-0061's "the
toolchain carries the default" is true only for links the driver runs. The
`direct-ld=sub-granule` verdict on the baseline is that gap, measured.

## What the baseline said

`results-2026-09-07.txt` is the first run, on the toolchain as it stood at
`b6f1fb4`, with neither candidate built. Four of the five probes came back as
the argument for D predicted, and the fifth was not about the layer question at
all.

`direct-ld=sub-granule`. A link the gcc driver never ran is not
granule-separable, which is DR-0061's bzip2 case reproduced on demand rather
than recalled: the specs file cannot reach a Makefile that calls `ld` itself.
`no-specs=default-lost` says the same thing from the other side — the default
lives entirely in that file — and `override=honored` says a per-link
`-z max-page-size=0x1000` still wins, so whatever carries the default must
leave DR-0008's own sub-granule fixture buildable.

`unwinder=lost-both`. The installed toolchain was still passing neither
`--eh-frame-hdr` nor `-lgcc_s` when this ran, because DR-0108 landed in the
tree and `install-specs` had not been run against the prefix. That is not a
finding about A or D; it is spike 53 reproduced by a second instrument, and it
is the reason this line is worth keeping in the transcript.

## What this does not reach

The rebuilds themselves. This measures prefixes; it does not build them, and
the patches for A and D are not written here — writing a patch nobody has
compiled would be the same mistake in a different file.

Whether either candidate costs something neither the probes nor DR-0061 name.
Five probes are five questions, and the specs file taught this project that the
expensive losses are the ones nobody thought to ask about. A full diff of the
`collect2` line and of the linker's own defaults, across the three prefixes,
would be the stronger instrument.

The runtime half. Every probe here reads a link, and a granule-separable image
is a claim the ELF mapper settles at load time, not one `readelf` settles.
