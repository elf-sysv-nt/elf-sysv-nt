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
file aside and asks again, since a candidate that needs the specs file has not
replaced it; the file is restored immediately, and `-B` cannot stand in for
moving it, because a `-B` prefix is searched in addition to the installed path
rather than instead of it. `override` links with `-Wl,-z,max-page-size=0x1000`
and checks that the smaller page still wins, because DR-0008's own test builds a
sub-granule image on purpose and a default that could not be overridden would
disarm the test of the layer that is the real guarantee. `unwinder` restates
DR-0108's property: `--eh-frame-hdr` and `-lgcc_s` are still passed.

A candidate carries the default when `no-specs` holds and `override` is honored.
Whether `direct-ld` is also granule-aligned is what separates the two layers,
and it is the whole of the bzip2 argument.

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
