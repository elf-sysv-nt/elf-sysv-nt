# What shape are el8's own binaries?

WP-10 has to fix four values that end up compiled into every artifact this
project ships, and three of them are only defensible against what Red Hat
already shipped rather than against a recollection of what Linux does. So the
binaries were read instead of remembered.

Ran 2026-08-29 on the RHEL-8.10 emulation, against 41 ELF files from three
pinned el8 packages: `glibc-2.28-251.el8_10.40`, `rpm-4.14.3-32.el8_10`, and
`rpm-build-4.14.3-32.el8_10`. The transcript is `results-2026-08-29.txt` and
`measure-shape.sh -D <dir>` regenerates it.

## What it found

`EI_OSABI` is not one value, and that is the finding. Thirty-six of the
forty-one objects carry `ELFOSABI_NONE`; the five that carry `ELFOSABI_GNU`
are `libc`, `libm`, `libmvec`, `ld.so`, and `ldconfig` — glibc's own, and
nothing else. The byte is emitted by the linker when an object uses
`STT_GNU_IFUNC` or `STB_GNU_UNIQUE`, so it describes the object rather than
the platform, which means WP-10 cannot pick a value for it and should not try.

The `.note.ABI-tag` payload is uniform: forty of forty-one say Linux 3.2.0.
The odd one out carries no note at all.

`PT_INTERP` says `/lib64/ld-linux-x86-64.so.2` in all fifteen objects that
have one, and the loader's own `DT_SONAME` is `ld-linux-x86-64.so.2`. Both
strings are load-bearing for WP-41, which reads the first, and for WP-53,
which has to produce a library the second names.

Every object is `ET_DYN`. The sample is small and the packages were chosen for
other reasons, so read this as absence of a counterexample rather than as a
claim that el8 ships no non-PIE executables; el8's hardening macros default to
PIE, and the exceptions are the packages that opt out.

## The two riders

Both are items the implementation plan lists as unverified, and the tree this
spike unpacks answers them at no extra cost.

Every `PT_LOAD` in the set — all eighty-two of them — has `p_align` 0x200000.
So the 2 MB case spike 2 priced as the awkward one is the only case, and
WP-41's constraint that a non-PIE span be reserved before anything else
allocates bites always rather than sometimes. Against that, no non-PIE image
turned up here to need it, which is the more useful half of the answer and the
half that wants a wider sample before anyone leans on it.

el8's `rpm-build` carries `/usr/lib/rpm/elfdeps`, sixteen kilobytes of it, and
ten files under `fileattrs` including `elf.attr`. The omission WP-62 attributes
to Cygwin's port does belong to the port. That package is confirmation rather
than work, as the plan guessed.

## Not verified

The PIE result. Three packages is not a sample of the el8 set, and the two
that are not glibc were picked because spike 4 had already pinned them.
Whoever needs the non-PIE share should widen `packages.tsv` rather than cite
this.

Whether `readelf` 2.29 reports `EI_OSABI` the way a later one would for an
object neither existed to see. Nothing here depends on it, since the values
found are the two that predate both.
