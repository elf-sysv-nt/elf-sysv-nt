# The target definition

Five values have to agree, and this is where they are written down. The triple
was settled by the operator in DR-0001. The other four were settled here on
2026-08-29, against measurement rather than against a recollection of what
Linux does: `spike/vendor-image-shape/` read forty-one el8 binaries and its
transcript is what the arguments below cite.

Every one of these ends up compiled into a shipped artifact. A `.note.ABI-tag`
sits in every executable, the loader SONAME sits in every `PT_INTERP`, and the
triple sits in every installed path, so changing one after packages exist means
rebuilding them. That is the whole reason this document precedes the toolchain
rather than falling out of it.

| Value | Setting |
|---|---|
| Target triple | `x86_64-elfsysvnt-linux-gnu` |
| `EI_OSABI` | `ELFOSABI_NONE`, or `ELFOSABI_GNU` where the object earns it |
| `.note.ABI-tag` | `GNU`, `NT_GNU_ABI_TAG`, Linux, 3.2.0 |
| Loader SONAME | `ld-linux-x86-64.so.2` at `/usr/lib64` |
| `uname` | `Linux` / `4.18.0-elfsysvnt` / `#1 SMP elfsysvnt` / `x86_64` |

## The triple

`x86_64-elfsysvnt-linux-gnu`, fixed by DR-0001 on 2026-08-29 and priced by
spike 5 the same day at one affected package in 2893. The argument for the
vendor field is in that record and is not restated. The four fields are
`cpu-vendor-kernel-os`, which is `config.sub`'s own naming and not this
document's: it sets `kernel=linux` and `os=gnu`, then validates the pair under
`case $kernel-$os-$obj`. Nothing is called `abi`. What each of the two claims,
and where one of them stops, is below under the limit of the `linux` claim.

What belongs here is the set of spellings derived from the triple, since those
are what a later package hardcodes:

    sysroot          /usr/x86_64-elfsysvnt-linux-gnu/sys-root
    tool prefix      x86_64-elfsysvnt-linux-gnu-
    gcc --target     x86_64-elfsysvnt-linux-gnu
    rpm %{_target}   x86_64-elfsysvnt-linux-gnu

The tool prefix is the honest one rather than a shortened alias. A second
spelling for the same target is how a build ends up half cross-compiled.

## The limit of the `linux` claim

Settled by DR-0005 on 2026-08-29, and written here because this is the document
every later package cites.

`gnu` names glibc, and it is true with nothing subtracted. Everything this
project ships is ELF, System V, versioned, and reaches `libc.so.6` through
`ld-linux-x86-64.so.2`. That is not a field tolerated because configure reads
it; it is the accurate one.

`linux` names the Linux kernel ABI, and this project means it: system call
numbers, `futex`, `clone`, `/proc`, the auxv a process is entered with, the
`uname` strings below. One item on that list is not delivered. A toolchain
reading `linux` assumes a `syscall` instruction reaches a kernel, and here it
does not, because `doc/elf-technical-breakdown.md`'s second bridge rebuilds
each package against `elfsysv1.dll` instead of catching anything. An object
that reaches the kernel through a raw `syscall`, rather than through a call
into the runtime, is outside the contract the triple advertises. No field of
any triple expresses that restriction, which is why it is written down in prose
rather than encoded in a name.

Two consequences, and the second one is the reason this section exists at all.
The kernel field is not a lie and must not be described as one, because the
only replacements are a libc field `config.sub` refuses outright and a kernel
field that costs a gcc, binutils and glibc port; DR-0005 carries the
measurement. And the vendor binaries this platform exists to run were compiled
under the unbounded claim, so their raw syscalls sit exactly on the axis where
ours stops. `doc/proposals/0003-vendor-binary-tls-rewriting.md` handles the
TLS half of that problem and the syscall half is not yet anybody's work
package.

## EI_OSABI

There is no value to pick, and discovering that is the useful half of this
section. The byte describes the object, not the platform: a linker writes
`ELFOSABI_GNU` when the object it produced contains `STT_GNU_IFUNC` or
`STB_GNU_UNIQUE`, and `ELFOSABI_NONE` otherwise. el8 bears this out exactly.
Of forty-one vendor objects measured, thirty-six carry `ELFOSABI_NONE` and the
five carrying `ELFOSABI_GNU` are `libc`, `libm`, `libmvec`, `ld.so`, and
`ldconfig` — glibc's own, and nothing besides.

So the rule, and WP-12 implements it rather than choosing it: emit
`ELFOSABI_NONE` by default, promote to `ELFOSABI_GNU` on the same trigger
upstream `bfd` already uses, and change nothing about when that trigger fires.

The loader has the matching obligation, and it is the reason this is written
down at all. WP-31 accepts 0 and 3 and refuses everything else with a
diagnostic naming the byte. Any private value would have been satisfying and
fatal: our own objects would carry it, vendor objects would not, and the first
`dlopen` of a Red Hat library would fail a gate we invented.

## The .note.ABI-tag

`GNU`, type `NT_GNU_ABI_TAG`, sixteen bytes of payload: operating system 0
(Linux), then 3, 2, 0. Byte-identical to what el8 emits, which forty of the
forty-one measured objects carry and the forty-first carries not at all.

The note is a minimum-kernel claim that a real `ld.so` checks against the
running kernel, and it points both ways. Our loader reads it in vendor
binaries, so we must satisfy 3.2.0. A real `ld.so` may one day read it in ours,
during a WP-T4 comparison or in somebody's mixed tree, so emitting el8's own
value keeps that door open for free.

The honest name does not go here. DR-0001 reserved the note as one of the
places the project name would move to under a masquerade, and the masquerade
did not happen; putting the name in a field with a defined meaning and an
active consumer would break the check to carry a string. Where the name does go
is below.

Delivery belongs to the startup files rather than to the linker, following
glibc, whose `abi-note.o` is linked into every program by `crt1.o`. WP-14 owns
emitting it. WP-12's acceptance test predates any startup file and therefore
assembles the note by hand, which is enough to prove the section survives
linking and lands in a `PT_NOTE`.

## The dynamic linker SONAME

`ld-linux-x86-64.so.2`, installed at `/usr/lib64/ld-linux-x86-64.so.2`, with
`/lib64` a symlink to `usr/lib64` in the el8 manner.

This one is forced, and there was never a choice to make. Fifteen of the
measured objects carry a `PT_INTERP` and all fifteen say
`/lib64/ld-linux-x86-64.so.2`. Every el8 binary in existence says it. A
distinct name would mean rewriting `PT_INTERP` in every vendor binary before
running it, which is the opposite of the premise this project rests on.

Two consequences follow. The file has to exist as a real ELF shared object,
because things `stat` it, `ldd` prints it, and rpm generates a `Provides` from
it; WP-41 short-circuits the interpreter for images it launches itself, but a
short-circuit in the spawn path is not an excuse for an absent file. And it is
a veneer in the same sense `libc.so.6` is: the loader's body lives in
`elfsysv1.dll`, and this object is the ELF-shaped face of it. WP-53 builds
both the same way.

The companion SONAMEs are Linux's throughout, and WP-54 lists them. Nothing in
the veneer renames anything.

## What uname reports

    sysname     Linux
    nodename    the host name, as Cygwin already reports it
    release     4.18.0-elfsysvnt
    version     #1 SMP elfsysvnt
    machine     x86_64
    domainname  (none)

`sysname` is `Linux` and there is no version of this project where it is not.
Thousands of configure scripts, `config.guess` among them, branch on this
string, and it makes the same bounded claim the triple's kernel field makes:
the Linux kernel ABI, satisfied by rebuild rather than by syscall dispatch. The
two have to agree, or a package cross-compiles against one answer and runs
against the other.

`release` is where the honest name goes, and it is the interesting choice on
this list. Every parser that reads a kernel version stops at the first
non-digit, so `4.18.0-elfsysvnt` compares equal to el8's own `4.18.0` for every
gate that matters, while a human running `uname -r` sees immediately what they
are on. The numbers are el8's because the packages are el8's: a package that
tests for a 4.18 kernel feature gets the answer its own distribution would
give it.

That is a claim about behavior rather than about capability, and it is worth
saying plainly. We do not have a 4.18 kernel. We have a runtime that answers
the questions el8's userland asks, and a package that goes looking for a kernel
interface we did not implement fails at the call rather than at the version
test. Which call it fails at is the bound above: a runtime entry point nobody
has written yet returns an error, and a raw `syscall` instruction has no
runtime to reach. Moving the version down would not make it fail earlier; it
would only make it fail differently, in the packages that gate correctly.

`version` carries the name again and nothing else. No build date, because a
transcript that changes every run is a transcript nobody diffs. No runtime
counter either: WP-25's API major and minor are read through an interface built
for the purpose, not scraped out of a string field.

`/proc/sys/kernel/ostype`, `/proc/sys/kernel/osrelease`,
`/proc/sys/kernel/version` and `/proc/version` are generated from this same
table and must never be written out separately. Two tables drift; one does not.

`uname -o` is not on this list because it never comes from `utsname` — GNU
coreutils compiles it in — so `GNU/Linux` there is a build-time setting and
belongs to WP-16 with the rest of the macro set.

## Where the name actually lives

Not in `EI_OSABI`, not in the ABI-tag note, not in the loader SONAME, and not
in either of the triple's two load-bearing fields. All are read by consumers
with a definite expectation, and in all of them a truthful string costs a
broken check or a broken build. The vendor field and `uname -r` are the two
places a name can sit without a consumer already depending on it, which is why
those are where DR-0001 and this document put it.

It goes in a note of its own: owner `ELFSYSVNT`, type 1, in a section named
`.note.elfsysvnt.abi`. The payload is two 32-bit words, the API major and the
API minor that WP-25 defines, and this document fixes only the carrier so that
WP-25 can settle the numbers without also having to settle where they sit.
Nothing existing reads an unknown note owner, so the cost is thirty-two bytes
per object and no compatibility surface at all.

## Done when

The plan's criterion is that every later package citing one of these values
cites this document rather than a memory of it, which is a judgment until
somebody makes it mechanical. `bin/check-target-definition` makes it
mechanical: it greps the tree for each literal, and reports any file carrying
one that does not also name `doc/target-definition.md`. It exits non-zero on
the first unattributed site.

## Not verified

That el8 ships no non-PIE executables. Every object in the sample was `ET_DYN`,
but the sample was three packages picked for other reasons, and this document
does not depend on the answer. WP-41 does.

That `ELFSYSVNT` is unclaimed as a note owner. Nobody has grepped a
distribution for it. The consequence of a collision is small and the check is
cheap, so it is listed rather than done.

The `release` string against real packages. The argument above is from how
version parsers are written, not from having built anything against it. The
first package that gates on a kernel version is where it gets tested, and if
the suffix turns out to bother `rpm`'s own comparisons, the escape hatch is
`4.18.0-1.elfsysvnt` rather than a return to a bare number.
