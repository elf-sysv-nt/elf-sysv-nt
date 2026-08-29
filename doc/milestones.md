# Milestones

The first five milestones are spikes, and none produces shippable code. That is
deliberate. `elf-technical-breakdown.md` ends with a list of claims that were
recalled rather than measured, four of them carry weight, and building on an
unmeasured claim is how a program discovers in year two that it chose wrong in
month one. Four of the spikes gate something. The fifth prices a naming
decision that has since been taken without it. Three have now run, and one of
them took the recommended path off the table, which is what a spike is for.

Each has a directory under `spike/` and one question it answers, yes or no for
the first four and a count for the fifth. The verdict is the deliverable.
Reaching one is a successful outcome even when the answer is unwelcome, and
especially then.

In dependency order, which is also cost order.

| # | Spike | Question | Gates |
|---|---|---|---|
| 1 | `spike/fs-base-persistence/` | Does Windows preserve a user-written FS base across a context switch? | The TLS layer, and the toolchain target through it. Run 2026-08-29: no. |
| 2 | `spike/map-and-jump/` | Can a PE stub map a static ELF and jump to it? | Image mapping and the initial process image. Run 2026-08-29: yes, with a constraint on when the span is claimed. |
| 3 | `spike/abi-crossing/` | Can one entry point be System V-faced over an MS-ABI core, through a signal? | `elfsysv1.dll`, and the `-mno-red-zone` policy |
| 4 | `spike/versioned-libc/` | Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized `libc.so.6`? | Nothing downstream, which is the point |
| 5 | `spike/triple-fidelity/` | How many packages in the el8 set mishandle a nonstandard vendor field? | Nothing. It priced DR-0001 rather than gating it. Run 2026-08-29: one, `flac`. |

Spike 1 was an afternoon and it decided a layer, against us. Spike 2 came back
yes and moved a question from whether to when. Spike 3 is the expensive one,
and a no there sends the program to the veneer-thunk fallback, which is a
different program. Spike 4 gates nothing technically; it measures whether the
whole edifice repairs what it was built to repair, and it should run before
anything large is funded. Spike 5 gated nothing in the end: the triple was
decided on 2026-08-29 without waiting for it, and the count it produced the
same day is the size of the patch set that decision commits to.
One package, well inside the threshold DR-0001 set in advance.

## Spike 1, the thread pointer

Run 2026-08-29, and the answer is no. `WRFSBASE` is available on this host and
a base written with it addresses `%fs:0` correctly, so the failure is not at
the instruction. It is at the scheduler: anything that takes the thread off a
processor returns it with the base at zero, and the probe's cases that block,
yield, migrate, take a signal or get hijacked all fail on their first or second
check.

The case that settles what it costs makes no system call at all. It spins on
`RDFSBASE` while a burner sits on every processor, and it still loses the base,
in tens of milliseconds. A base cleared on the way back from a call could have
been re-established at the call site; a base cleared by preemption cannot,
because preemption has no call site. So the two obvious repairs are both closed
and this reaches the toolchain layer rather than merely adding work to it, as
the milestone said it would.

`spike/fs-base-persistence/results-2026-08-29.txt` is the transcript and that
spike's README reads it. What replaces `%fs` is reserved to the operator by
`AGENTS.md`, and no fallback is picked here.

## Spike 2, mapping and jumping

Run 2026-08-29, and the answer is yes. A PE stub reserved the span a static
`ET_EXEC` asks for, committed and protected one region per `PT_LOAD`, built
the stack the psABI describes, and jumped to `e_entry`; the image ran at its
link address, read its own segments, walked the auxv it was handed, and
returned. Three of the four mapping cases did that, both controls were
refused, and every protection Windows reported back was the one asked for --
confirmed by fault probes rather than only by `VirtualQuery`, because a spike
that reads back its own request has measured its own request.

Two findings come free with it. `.bss` costs nothing, because Windows hands
back freshly committed pages already zeroed and the word past `p_filesz` read
as zero without the stub touching it. And a link base that is page-aligned but
not granule-aligned costs address space rather than correctness: an image at
`0x8048000` reserves from `0x8040000` and spends 32 KB below itself on
nothing.

The fourth case is the finding that matters. At a `p_align` of `0x200000` --
`ld`'s default, and so what a vendor binary is expected to carry -- each
segment lands on its own 2 MB boundary and the span inflates from 24 KB to
4 MB without gaining a byte of content. At `0x400000` that span is not
available: the free run there measured between `0x200000` and `0x260000`
across runs, never the `0x405000` wanted, and the reservation was refused
twenty times in twenty. The same geometry at `0x10000000` mapped and ran, so
the arithmetic is right and the address is the problem.

What takes the low addresses is Windows' bottom-up allocator, and the spike
caught it in the act: in the case that mapped high, the stub's own stack
allocation, requested with no base, came back at `0x400000`. Anything in the
process that allocates before the image's span is claimed can take part of it,
and a Cygwin runtime allocates before `main`.

So the question moves from whether to when, and it lands on WP-41: a non-PIE
image's span has to be reserved before the runtime under the stub warms up.
Whether a PE TLS callback or an image entry point is early enough is not
measured here, and it should be measured before that package is written rather
than discovered inside it. `spike/map-and-jump/results-2026-08-29.txt` is the
transcript and that spike's README reads it.

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

It came back a handful, and a small one. Over all 2893 source names, the
masquerade and the candidate get identical verdicts from every one of the 1193
vendored `config.sub` files, and exactly one package carries a live literal
host test: `flac`, whose `configure.ac` gates `FLAC__SYS_LINUX` on
`*-pc-linux-gnu)`. The transcript is
`spike/triple-fidelity/results-2026-08-29.txt` and that spike reads the
eighteen other matches, which are comments and test fixtures.

Path length is settled and needs no spike. On this machine the deepest
installed path carrying a triple is a libstdc++ policy header at 132 characters
POSIX, 146 as Windows sees it through the Cygwin root; the ten characters the
longer vendor adds put the worst case at 156, against a `MAX_PATH` of 260. A
build tree is deeper than an installed tree and a gcc bootstrap stacks stage
directories on top, but a hundred characters of headroom absorbs that. Measured
2026-08-20.

## After the spikes

Unscheduled, because spike 3's answer can still reshape it. `ROADMAP.md`
inventories the work and `IMPLEMENTATION-PLAN.md` cuts it into packages; the
sketch below is the shape both of them fill in.

The target triple wanted deciding before the first package was built, and it
was, on 2026-08-29. The TLS model now wants deciding on the same footing, and
for the same reason: spike 1 came back no that afternoon, the replacement is
the operator's call, and the toolchain cannot be configured around a thread
pointer nobody has named. The toolchain follows, which is otherwise routine
cross-toolchain work. Then the loader, where musl's `dynlink.c` is the working
model and the verdef and verneed matcher is the part musl leaves out.
`elfsysv1.dll` and the libc veneer come after the loader can run something. The
`r_debug` rendezvous can start as soon as there is a loader to announce
objects, and it should, because the alternative is debugging a world Windows
tools cannot see.
