# config.sub and config.guess

`config.sub` needs no patch. That is the useful finding and it was checked
rather than assumed: upstream at timestamp `2026-05-17` canonicalizes
`x86_64-elfsysvnt-linux-gnu` to itself, byte for byte, exit 0. DR-0001 argued
the vendor field would cost nothing here and it costs nothing here.

`config.guess` needs one, because it has to produce the triple rather than
merely accept it, and it cannot read the answer off `uname`. Our `uname -s`
says `Linux`, permanently and on purpose, so an unpatched `config.guess`
returns `x86_64-pc-linux-gnu` and a native build configures for a vendor we
are not. The patch asks the compiler instead, which is how `config.guess`
already separates uclibc from dietlibc from glibc twenty lines earlier.

    0001-config-guess-name-the-elfsysvnt-vendor.patch

It touches the x86_64 Linux arm and nothing else. Four cases were run against
GNU config `2026-05-17` on 2026-08-29:

| Case | Result |
|---|---|
| unpatched, ordinary cc | `x86_64-pc-linux-gnu` |
| patched, ordinary cc | `x86_64-pc-linux-gnu` |
| patched, cc defining `__ELFSYSVNT__` | `x86_64-elfsysvnt-linux-gnu` |
| patched, no compiler, `/etc/elfsysvnt-release` present | `x86_64-elfsysvnt-linux-gnu` |

The second row is the one that matters as much as the third. A patch that
changed the answer on an ordinary host would be rejected upstream and would
break every other build on the machine.

Two things follow for later packages. WP-13 has to define `__ELFSYSVNT__` in
the specs, alongside the rest of what the target mandates rather than
suggests. And WP-63 has to install `/etc/elfsysvnt-release`, since
`config.guess` runs in trees with no compiler and that file is the only answer
available there. The values it agrees with are in `doc/target-definition.md`.

## The refresh, which is the actual work

891 of the 2893 el8 source packages carry a vendored `config.sub`, 1193 copies
between them, each frozen at whatever vintage its last release captured. Spike
5 read every one of them and the news was good: all 1193 treat the honest
triple exactly as they treat `x86_64-pc-linux-gnu`, and the twenty that refuse
it refuse the masquerade too, on the cpu field, because they predate x86_64.

So the policy is about vintage, not about vendor, and it is the policy every
distribution already runs: put a current pair in the tree before configuring.

    refresh-config [options] TREE...

Reseeded, never edited. Each copy is replaced outright from a pristine pair
built once from `upstream.pin` plus the patch above, so what a run produces is
a function of the pin rather than of what the last run left behind. The seed
itself is rebuilt whenever the pin or the patch changes, which is the same
rule applied to the tool's own cache.

`--check` reports staleness and writes nothing, exiting 1 when any copy
differs; it belongs in CI. `--dry-run` names what would change. Symlinked
copies are left alone, since automake and libtool both point `build-aux` at a
single real file and replacing the link would break the package's bookkeeping
once per link. Leftovers from an interrupted run, `.orig` and `.rej` and
`.new` and editor backups, are removed rather than merely not written.

Verified on 2026-08-29 against a tree shaped like the awkward real ones: a
2009 pair at the root, a 2012 pair under `build-aux`, a symlinked third, two
leftovers, and a copy left mode 644. One run reseeded four files, reaped two,
left the symlink, and set mode 755. The second run wrote nothing and left
identical checksums, which is the exit criterion.

## The one package that needs a patch of its own

`flac`, and only `flac`. Its `configure.ac` gates `FLAC__SYS_LINUX` on
`*-pc-linux-gnu)`, so any other vendor loses the define and builds as though
the platform were unknown. The fix matches the os field instead, which is what
the test meant, and it is in `../patches/flac-sys-linux-any-vendor.patch`.

Spike 5 found eighteen other literal vendor matches across the set and read
every one; they are comments and test fixtures. One package in 2893 is well
inside the band DR-0001 wrote down before the count, so nothing here reopens
the triple.

## Not verified

That the pin still resolves. `upstream.pin` points at cgit's `plain/` path,
which serves whatever is current rather than a fixed revision, so a checksum
miss means upstream moved and the patch wants rechecking against the new file.
That is the intended behavior and it will happen; it is listed so that whoever
meets it does not read it as a broken download.

That the twenty packages whose `config.sub` refuses the triple are fixed by a
refresh. It is the obvious inference, since they refuse it for predating
x86_64 and the refresh replaces the file, but nobody has run `configure` in
one of those trees afterward. `perl-Tk` and `autoconf213` are the ones to try.

Whether any package's build system rejects a `config.sub` newer than the
`configure` beside it. Automake has warned about version skew in the past.
Nothing in the el8 set has been built yet, so this is a thing to watch for
rather than a thing observed.
