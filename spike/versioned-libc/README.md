# The payoff

Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized
`libc.so.6` carrying one verdef node? Yes, and the string is not merely the
right shape, it is byte for byte the string the vendor's own library provides
and the string an el8 binary asks for. `results-2026-08-29.txt` is the
transcript and the section below it is the reading.

Three scripts take the measurement. `fetch-elfdeps.sh` stands el8's own
dependency generator up on a host that is not el8, `synth-libc.py` writes the
library and the program that needs it byte by byte, and `probe-elfdeps.sh`
points the one at the other and classifies what comes back. What is kept here
is the means of taking it again.

This spike gates nothing downstream, which is the point of running it early.
The other three ask whether the thing can be built. This one asks whether
building it repairs what it was supposed to repair, and it should be answered
before anything large is funded rather than after.

## Why the answer was in doubt

`doc/symbol-versioning-formats.md` is the record this spike exists to close.
Its finding is that PE has no representation for symbol versioning, that no
alternative image format is reachable on Windows, and that rpm has no PE
dependency generator and never will have a useful one: it could emit a
module-level requires against a DLL name and could never emit a version node,
because the file does not contain one. Every el8 package linking libc carries
requires of the form `libc.so.6(GLIBC_2.2.5)(64bit)`, so under that route the
dependency comparison against the vendor differs structurally and permanently,
on nearly the whole package set. One accepted deviation, four hundred packages
wide.

Writing our own loader and keeping ELF is what buys that back, and the whole
purchase reduces to whether an ELF file we synthesized reads, to rpm, like an
ELF file glibc shipped. It is a cheap thing to measure and an expensive thing
to be wrong about.

## Running it

    ./fetch-elfdeps.sh --dest /path/to/work
    ./probe-elfdeps.sh --dest /path/to/work -o results-$(date +%F).txt

The fetch wants a network and about nine megabytes; the probe wants neither
and takes a second. `--terse` prints the summary block alone, one `key=value`
per line, which is the form to quote in a document. Rerunning either is safe:
the derived trees are cleared and rebuilt each time, and the archives are
reused while their checksums still match.

`t/run-tests.sh` needs no network, no el8 and no mirror. It checks that the
emitter is byte-reproducible against a recorded checksum, reads the emitted
files back with a parser written separately from the emitter so that a field
put at the wrong offset is caught rather than round-tripped, and checks what
both scripts refuse. Twenty checks. The reproducibility one was watched going
red, by editing one character of the recorded checksum, before it was trusted
going green.

## Where it ran, and why not on el8

On WSL2 under Ubuntu 26.04, running el8's own `elfdeps` binary out of
`rpm-build-4.14.3-32.el8_10` with librpm, librpmio, libelf and the dozen
libraries beneath them unpacked from the same el8 set and put in front of it as
`LD_LIBRARY_PATH`. What it borrows from the host is glibc alone, and only
forward: a binary linked against 2.28 runs on 2.43, which is the direction
glibc guarantees.

That is one step short of el8 itself and the step is named here rather than
glossed. Nothing `elfdeps` does for this question touches the host libc -- it
opens a file, walks its sections with libelf, and formats strings -- so the
answer is not expected to move on a real el8 box. Expected is not measured, and
it stays in Not verified until someone runs the same script there.

The el8 glibc is fetched too and is deliberately kept off that loader path. Its
`libc.so.6` is an input to the probe, the thing our library is compared
against, and a 2.28 libc on the loader path of a 2.43 host breaks every process
the run starts.

Nothing wants root. No `rpm`, no `rpm2cpio` and no `cpio` are wanted either,
since a host carrying them would probably be el8 already; `rpmx.py` walks the
rpm header chain and the newc payload in Python. Every package is pinned by
checksum in `packages.tsv` and a mismatch stops the run.

## What is synthesized, and why not compiled

`synth-libc.py` lays the files out field by field rather than calling a
compiler. A library `gcc` and `ld` produced would answer an easier question:
whether the GNU toolchain still works. What this project will eventually ship
is a library it synthesized, so that is what gets put in front of the
generator.

The library is an `ET_DYN` object with a load segment, a dynamic segment, both
hash tables a modern DSO carries, `.dynsym`, `.dynstr`, `.gnu.version` and
`.gnu.version_d`. `readelf -d` and `readelf -V` parse it, `file` calls it a
shared object, and it is thirteen hundred bytes. The consumer is the matching
`ET_EXEC`: a `PT_INTERP`, `DT_NEEDED` on the library, and a `.gnu.version_r`
asking for the node the library defines.

Both are reproducible byte for byte, which is what makes the recorded checksum
a test rather than a note.

## The verdict, 2026-08-29

Yes, on all four measurements, and the fourth is the interesting one.

**The string.** A synthesized `libc.so.6` carrying one verdef node produces
exactly two provides, `libc.so.6(GLIBC_2.2.5)(64bit)` and
`libc.so.6()(64bit)`. The first is byte-identical to the first line el8's own
`libc-2.28.so` produces through the same generator.

**The other half.** The synthesized consumer produces
`libc.so.6(GLIBC_2.2.5)(64bit)` as a requires, which is the same string again.
So the edge closes: what the library offers is what the program asks for, and
rpm would resolve one against the other. A generator that spelled the two ends
of one edge differently would satisfy nothing, and one node is enough to see
it either way.

**The ladder.** Synthesized again with el8 libc's whole node list -- 29 of
them, `GLIBC_2.2.5` through `GLIBC_2.28` and `GLIBC_PRIVATE` -- the provides
set is identical to the vendor's, thirty lines each, sorted and uniqued. Not
similar: identical. So one node proves the mechanism and the ladder prices the
veneer. This matters because a package requiring `GLIBC_2.14` is not satisfied
by a library defining only `GLIBC_2.2.5`; `rpm` itself needs three of the
nodes, and all three are met by the ladder library.

**The trap.** A versioned provide is read off the base verdef node, not off
`DT_SONAME`. `processVerDef` takes the `VER_FLG_BASE` node's name and formats
every versioned provide against it, while the unversioned provide comes from
`DT_SONAME` later. In any library a linker produced the two strings are the
same and the question never arises. Synthesized, they can differ: a library
whose `DT_SONAME` is `libc.so.6` and whose base node says `libmisnamed.so.1`
provides `libmisnamed.so.1(GLIBC_2.2.5)(64bit)` and `libc.so.6()(64bit)`,
which satisfies no package and reports no error. That is measured here rather
than reasoned about, by a fixture built to get it wrong on purpose.

## Three things the source says that the veneer will have to honor

Read off `tools/elfdeps.c` in `rpm-4.14.3`, as el8 ships it, and confirmed
against the run.

An execute bit, not an object type, decides whether requires are emitted at
all. `isExec` is `st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)`, so a shared library
installed `0644` contributes no requires, and neither does a program. el8's
own `libc.so.6` is `0755` and both fixtures here are written `0755` for that
reason.

A soname that does not look like one is dropped in silence. `skipSoname`
requires the string to contain `.so` and to begin with `lib`, exempting the
dynamic linker's `ld.`, `ld-` and `ld64.` prefixes, and anything else is
discarded without a diagnostic. Whatever the runtime is called on its ELF face
has to fall inside that filter; a name shaped like `elfsysv1.dll` would vanish
from every dependency rpm generates.

`rtld(GNU_HASH)` is not read out of libc. `elfdeps` adds it to any executable
carrying `DT_GNU_HASH` without `DT_HASH`, and it is satisfied by a package-level
`Provides` that glibc declares rather than by anything in an ELF file. The
veneer's package will have to declare it. The fixtures here carry both hash
tables, as el8's libc does, so they do not raise it; el8's `rpm` binary does.

## What this does not settle

It measures rpm reading a file. It does not measure a loader, it does not
measure the veneer, and it says nothing about whether the symbols behind those
29 nodes can be implemented -- only that the metadata describing them is
producible and reads correctly. The mechanism is proved; the content is the
program.

It also does not settle that a package built on this platform acquires these
dependencies automatically. That needs an rpm on the *build host* carrying
`elfdeps` and `fileattrs/elf.attr`, and Cygwin's `rpm-4.18.0-1` ships neither,
as `doc/symbol-versioning-formats.md` records. So the stage 0.5 admission of a
dependency generator survives this verdict, changed in kind: it is a
build-host gap rather than a format impossibility, and the thing to install is
rpm's own generator rather than something written here.

One `Not verified` item elsewhere closes on the way past. el8's
`rpm-build-4.14.3-32.el8_10` does carry `/usr/lib/rpm/elfdeps` and ten
`fileattrs`, `elf.attr` among them, so the omission recorded against Cygwin's
rpm is the port's and not the version's.

## Where the finding goes

`doc/symbol-versioning-formats.md` keeps its accepted deviation, which was
always scoped to the PE route and is what this project exists to avoid.
`doc/milestones.md`, `doc/ROADMAP.md` and `doc/IMPLEMENTATION-PLAN.md` record
spike 4 as run. The base-node trap belongs in whatever eventually specifies the
veneer's `.gnu.version_d`, because it is the kind of mistake that produces a
package that installs and satisfies nothing.

## Not verified

Recorded so a later reader does not mistake these for measured.

The run was on WSL2 with el8's binary rather than on el8. Nothing in the code
path touches the host libc, which is a reading of the source rather than a
second measurement.

Only `libc.so.6` was synthesized. Other versioned libraries go through the same
two functions in the same file, so the same answer is expected and was not
taken.

Only `elfdeps` was run. Whether rpm's file-attribute dispatch, `rpmdeps` and
the solver agree with the raw generator was not measured, and the honest
statement is that the generator's output is right rather than that a build
would come out right.
