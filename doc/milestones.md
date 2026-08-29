# Milestones

The first five milestones are spikes, and none produces shippable code. That is
deliberate. `elf-technical-breakdown.md` ends with a list of claims that were
recalled rather than measured, four of them carry weight, and building on an
unmeasured claim is how a program discovers in year two that it chose wrong in
month one. Four of the spikes gate something. The fifth prices a naming
decision that has since been taken without it, which is the only change to
this list since it was written.

Each has a directory under `spike/` and one question it answers, yes or no for
the first four and a count for the fifth. The verdict is the deliverable.
Reaching one is a successful outcome even when the answer is unwelcome, and
especially then.

In dependency order, which is also cost order.

| # | Spike | Question | Gates |
|---|---|---|---|
| 1 | `spike/fs-base-persistence/` | Does Windows preserve a user-written FS base across a context switch? | The TLS layer, and the toolchain target through it |
| 2 | `spike/map-and-jump/` | Can a PE stub map a static ELF and jump to it? | Image mapping and the initial process image |
| 3 | `spike/abi-crossing/` | Can one entry point be System V-faced over an MS-ABI core, through a signal? | `elfsysv1.dll`, and the `-mno-red-zone` policy |
| 4 | `spike/versioned-libc/` | Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized `libc.so.6`? | Nothing downstream, which is the point |
| 5 | `spike/triple-fidelity/` | How many packages in the el8 set mishandle a nonstandard vendor field? | Nothing. It prices DR-0001 rather than gating it. |

Spike 1 is an afternoon and decides a layer. Spike 3 is the expensive one, and
a no there sends the program to the veneer-thunk fallback, which is a different
program. Spike 4 gates nothing technically; it measures whether the whole
edifice repairs what it was built to repair, and it should run before anything
large is funded. Spike 5 gates nothing either, now: the triple was decided on
2026-08-29 without waiting for it, so the count it produces is the size of the
patch set that decision commits to, read against the threshold DR-0001 sets in
advance.

## Spike 5, the target triple

The triple has four fields and they are not equally load-bearing. Only `os` and
`abi` are consulted by the machinery that would break; `vendor` is passed
through by `config.sub` untouched and read by almost nothing. So the triple is
`x86_64-elfsysvnt-linux-gnu`, decided on that reasoning and recorded in
DR-0001: it puts the honest name where it costs least and leaves `linux-gnu`
standing where configure actually looks. Buildroot
has shipped `x86_64-buildroot-linux-gnu` on the same grounds for over a decade,
and crosstool-NG ships `unknown` in that slot, so neither the shape nor the
length is novel.

What the vendor costs is the open question. A package that matches `*-linux-gnu`
is unaffected; one that matches the literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` misses, silently, and takes a configure branch nobody
intended. Such packages exist. How many is the size of the patch set the
decision commits to, and guessing at it is precisely the habit these spikes
exist to break.

The script takes the `config.sub` shipped by each package in the el8 source
set, feeds it three candidates, and records the canonicalized output of each:
the masquerade `x86_64-pc-linux-gnu`, the vendor-honest
`x86_64-elfsysvnt-linux-gnu`, and the os-honest `x86_64-pc-elfsysvnt` as a
control. The control was put there expecting a rejection, and a preliminary run
over nine local `config.sub` vintages says otherwise: pre-2020 files do not
validate the os field at all, and the 2021 file accepts `elfsysvnt` by matching
`elf*`, the entry that exists for bare-metal ELF targets. Silent acceptance
into `config.gcc`'s bare-metal branch is a worse outcome than a refusal, which
is the argument for the vendor field restated on firmer ground. The same pass
greps the set for literal vendor matches in `configure.ac`, `configure`, and
any hand-written `case $host`. The transcript is two counts and the offending
package names.

A verdict of a handful is a patch set. A verdict in the hundreds argues for the
masquerade, and the honest name moves to `EI_OSABI`, the `.note.ABI-tag`, the
dynamic linker SONAME, and `uname`, which is arguably where it belonged anyway:
those are read at runtime, by tools, whereas the triple is a build-time label
that no shipped artifact consults. Where the line between the two sits is in
DR-0001, as a share of packages rather than as an adjective, written before the
count so that the count cannot be read to suit.

Path length is settled and needs no spike. On this machine the deepest
installed path carrying a triple is a libstdc++ policy header at 132 characters
POSIX, 146 as Windows sees it through the Cygwin root; the ten characters the
longer vendor adds put the worst case at 156, against a `MAX_PATH` of 260. A
build tree is deeper than an installed tree and a gcc bootstrap stacks stage
directories on top, but a hundred characters of headroom absorbs that. Measured
2026-08-20.

## After the spikes

Unscheduled, because two of the five answers can still reshape it. `ROADMAP.md`
inventories the work and `IMPLEMENTATION-PLAN.md` cuts it into packages; the
sketch below is the shape both of them fill in.

The target triple wanted deciding before the first package was built, and it
was, on 2026-08-29. The toolchain follows, which is routine cross-toolchain
work. Then the loader, where musl's `dynlink.c` is the working model and the
verdef and verneed matcher is the part musl leaves out. `elfsysv1.dll` and the
libc veneer come after the loader can run something. The `r_debug` rendezvous
can start as soon as there is a loader to announce objects, and it should,
because the alternative is debugging a world Windows tools cannot see.
