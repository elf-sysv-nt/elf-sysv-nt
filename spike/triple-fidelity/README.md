# The vendor-field count

How many el8 packages mishandle a nonstandard vendor field? Nothing in this
directory answers that yet. The script takes the measurement on a tree of
unpacked el8 sources, and what is kept here is the means of taking it again.

The target triple has four fields and only two carry weight. `config.sub`
passes an unrecognized vendor through untouched, so `x86_64-elfsysvnt-linux-gnu`
puts the project's name where it costs least and leaves `linux-gnu` standing in
the fields configure actually reads. Buildroot has shipped
`x86_64-buildroot-linux-gnu` on that reasoning for over a decade. The residual
cost is the package that tests for a literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` and misses, silently, taking a branch nobody intended.
Such packages exist. How many is the number that decides the triple, and
guessing at it is the habit these spikes exist to break.

## Running it

Point it at a tree of unpacked sources, one package per top-level directory.

    ./count-vendor-misses.sh -r /path/to/el8-src -o results-$(date +%F).txt

Two probes over one walk. Each `config.sub` found is fed three triples and its
verdict recorded; the same walk greps for literal vendor matches in the places
a host test lives. Nothing is built, nothing is installed, no privilege is
wanted. Expect the grep to dominate the runtime on a large tree.

`--terse` prints the summary block alone, one `key=value` per line, which is
the form to quote in a document. `--verbose` names every offending package
rather than only counting it, which is what you want on the first run and not
on the twentieth.

## Reproducing it away from the tree

`--keep-dump FILE` writes the raw probe as the walk runs, and `--dump FILE`
classifies a recorded probe instead of walking anything. A transcript can then
be regenerated from the same bytes months later, on a machine that has no el8
sources at all.

## The fixture

`t/run-tests.sh` classifies `t/sample-dump.txt` and compares the summary
against counts worked out by hand. Four synthetic packages cover the shapes
that matter: a clean accept, a silent rewrite, an outright rejection, and a
package with no `config.sub` at all but a literal vendor in its sources.

## A preliminary run, and a correction it forced

`results-preliminary-2026-08-20.txt` is not the verdict. It is the script run
against the only `config.sub` files on this machine, nine of them, spanning
automake 1.9 through 1.16 and newlib-cygwin, with timestamps from 2009 to
2021. All nine accepted the candidate byte for byte, which is the expected
result and worth little on its own.

All nine also accepted `x86_64-pc-elfsysvnt`, the os-honest control, and that
was not expected. Two separate reasons, and the second is the interesting one.

The 2013 and 2019 vintages do not validate the `os` field at all, so an unknown
os passes as readily as an unknown vendor. Strict validation arrived with the
2020 rewrite. That much only says the door is unlocked on the vintages el8
ships.

The 2021 `config.sub` in the newlib-cygwin tree does validate, rejects `nt` and
`notarealos` outright, and accepts `elfsysvnt` anyway. It matches `elf*`, an
entry in the recognized-os list that exists for bare-metal ELF targets of the
`i386-elf` kind. Acceptance is an accident of a glob rather than a grant, and
it is worse than a rejection would have been: gcc's `config.gcc` reads
`x86_64-*-elf*` as bare metal, so an os field beginning with `elf` would be
silently routed to a target definition that has no operating system under it at
all. A rejection is a build that stops; this is a build that succeeds and is
wrong.

So the case for the vendor field is stronger than the argument it was made on,
not weaker. It just does not rest where it was thought to. `config.sub`
refusing the triple at the door was never the hazard; the hazard is what
accepts it and what that acceptance then means downstream.

## Where the finding goes

`doc/elf-technical-breakdown.md`, in `The toolchain and the triple`, and the
`Not verified` entry that currently records the claim as uncounted. The number
decides whether the vendor-honest triple stands or the honest name moves to
`EI_OSABI`, the `.note.ABI-tag`, the dynamic linker SONAME, and `uname`.
