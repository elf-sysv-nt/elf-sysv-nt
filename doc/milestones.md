# Milestones

The milestones open with spikes, and none produces shippable code. That is
deliberate. `elf-technical-breakdown.md` ends with a list of claims that were
recalled rather than measured, four of them carry weight, and building on an
unmeasured claim is how a program discovers in year two that it chose wrong in
month one. Five were planned. Four of them gate something, the fifth prices a
naming decision that has since been taken without it, and the work added three
more as it went: a sixth that settled what `%fs` could not provide, a seventh
that priced whether the red zone can survive delivery, and an eighth that WP-12
turned up. Three more had run without a row until the design-gaps review
numbered them: a ninth that read el8's own binary shape, a tenth that caught the
linker emitting `%fs` code unasked, and an eleventh that built Cygwin from
source. All eleven have run, and one of them took the recommended path off the
table, which is what a spike is for.

Each has a directory under `spike/`, and most answer one question — yes or no
for all but the fifth, a count for that one. Two carry a second characterization
beside the first: `abi-crossing` measures the fault beneath a System V frame as
well as the signal crossing, and `map-and-jump` measures span-claim overlap as
well as the map-and-jump itself, so `test/spike-regen.tsv`, the authoritative
index, counts eighteen measurements across the sixteen directories. Both second
findings are recorded in their spike's entry below. The verdict is the
deliverable. Reaching one is a successful outcome even when the answer is
unwelcome, and especially then.

In dependency order, which is also cost order.

| # | Spike | Question | Gates |
|---|---|---|---|
| 1 | `spike/fs-base-persistence/` | Does Windows preserve a user-written FS base across a context switch? | The TLS layer, and the toolchain target through it. Run 2026-08-29: no. |
| 2 | `spike/map-and-jump/` | Can a PE stub map a static ELF and jump to it? | Image mapping and the initial process image. Run 2026-08-29: yes, with a constraint on when the span is claimed. |
| 3 | `spike/abi-crossing/` | Can one entry point be System V-faced over an MS-ABI core, through a signal? | `elfsysv1.dll`, and the `-mno-red-zone` policy. Run 2026-08-29: yes, and the red zone is lost to our own layer rather than to the host. |
| 4 | `spike/versioned-libc/` | Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized `libc.so.6`? | Nothing downstream, which is the point. Run 2026-08-29: yes, byte for byte. |
| 5 | `spike/triple-fidelity/` | How many packages in the el8 set mishandle a nonstandard vendor field? | Nothing. It priced DR-0001 rather than gating it. Run 2026-08-29: one, `flac`. |
| 6 | `spike/gs-thread-pointer/` | Does a thread pointer reached through `%gs` survive the switch that `%fs` did not? | The TLS model. Run 2026-08-29: yes, four carriers measured; DR-0003 took C3. |
| 7 | `spike/redzone-delivery/` | Can delivery reserve the red zone before it builds the handler frame? | Whether `-mno-red-zone` can be retired. Run 2026-08-29: yes, a reserved delivery holds it and the far side survives; the cost is WP-43's to price. |
| 8 | `spike/fs-base-fault/` | What does an access through a zeroed `%fs` base do, and can a handler resume from it? | Whether a load-time TLS rewriter for vendor binaries may be a heuristic. Run 2026-08-29: it faults and a handler resumes, over the data-movement forms and not the arithmetic ones. |
| 9 | `spike/vendor-image-shape/` | What shape are el8's own binaries — OSABI, ABI-tag, `PT_LOAD` alignment, SONAME? | WP-10's four compiled-in target values. Run 2026-08-29: measured against 41 el8 ELF files. |
| 10 | `spike/ld-tls-relaxation/` | Does the linker emit `%fs`-relative code on its own? | The binutils TLS-relaxation policy in WP-12. Run 2026-08-29: yes, so WP-12 refuses those relocations rather than rewriting them. |
| 11 | `spike/cygwin-from-source/` | Can this machine build `cygwin1.dll`, and does a reserving delivery hold the red zone? | WP-26's from-source build and the red-zone reservation. Run 2026-08-29: both recorded, the prerequisites and the reservation captured. |
| 12 | `spike/demand-census/` | How many el8 packages need a symbol the classification can only stub? | WP-56's slice order and its named acceptance package. Infrastructure landed 2026-08-31; the run over the 4855-name worklist is in progress. |
| 13 | `spike/reent-bringup/` | Which host shape makes a reent-consuming libc body work across the face? | WP-56's `reent-tls-bringup` road-to-green row. Run 2026-09-01: the real-process shape (crt0/`_dll_crt0`) carries it — a body sets `errno` through the caller's reent — while the cygload shape's bring-up call hangs; DR records the certification path. |
| 14 | `spike/reent-stub-link/` | Does relinking the loader stub in the real-process shape make it start? | WP-56's `reent-tls-bringup` road-to-green row, item 1. Run 2026-09-01: it links (once `-lgcc` supplies the builtins `-nostdlib` drops) but the standalone stub faults before entry; row 19 locates that fault as the crt0 `cygwin_internal` ABI crossing, not the window collision first supposed, and a decision record reframes item 1 as crossing the Microsoft↔System V ABI boundary rather than reconciling a window. |
| 15 | `spike/reent-veneer-runtime/` | Can the WP-53 `libc.so.6` veneer stand as the crossing's reent-bearing runtime? | WP-56's `reent-tls-bringup` road-to-green row, item 2. Run 2026-09-01: the veneer builds and carries the whole reent surface (the `errno@@GLIBC_PRIVATE` TLS carrier and `strtol` at its el8 node), but every FUNC/IFUNC body is a single-byte `ret` — the `elfsysv1.dll` forward each entry reaches is data in `libc-forward.tsv`, not emitted code — so it resolves the crossing at link time but consults no reent at run time; item 2 is generating the forwarding bodies, not merely building the veneer. |
| 16 | `spike/reent-veneer-body/` | What must a real forwarding body reach, and can a link-time forward reach it? | WP-56's `reent-tls-bringup` road-to-green row, item 2. Run 2026-09-01: every forward-map target (1047 `forward-same`/`forward-alias` names) is a real `elfsysv1.dll` export, so a body has a real destination; but a link-time forward (`jmp strtol@PLT` under the veneer's own `.symver`) self-references — the linker binds it to the veneer's own definition, not the PE export — so item 2's bodies are runtime-resolving thunks against the WP-27 crossing, not a link flag. |
| 17 | `spike/reent-veneer-thunk/` | What is the link-time shape of the runtime-resolving thunk that replaces the `ret` stub? | WP-56's `reent-tls-bringup` road-to-green row, item 2, the companion to row 16. Run 2026-09-01: a thunk that names its target `"strtol"` as `.rodata` and resolves it at run time through one hidden per-veneer resolver links a versioned ET_DYN where `strtol@@GLIBC_2.2.5` is DEFINED with a real 40-byte body, no undefined `.dynsym` entry or relocation names `strtol`, and the resolver stays out of `.dynsym` — so unlike the naive forward there is no ELF dependency on the faced name for the linker to self-bind. That fixes the codegen contract `generate.py` must emit; whether so-shaped a thunk reaches the face across the loader is item 3, deferred behind the built face and the WP-53 veneer. |
| 18 | `spike/reent-veneer-face-exports/` | Does every FUNC forward thunk key on a name the face actually exports, across the whole set? | WP-56's `reent-tls-bringup` road-to-green row, item 2, the standing guard for row 16. Run 2026-09-01: all 973 unique `forward-same`/`forward-alias` FUNC targets the built veneer emits are names in the committed `runtime/face/face.tsv` export table, so the run-time resolver finds a face export for each key. Row 16 checked this against the built `elfsysv1.dll` and so SKIPs in the regen harness; this restates it over the FUNC thunk set and the committed face table, needing only the cross toolchain, so it runs as a continuous guard. The name axis of item 2 is complete; reaching the face across the loader stays item 3. |
| 19 | `spike/reent-stub-realproc-window/` | Where does the real-process relink of the loader stub fault, and is it the window collision row 14 first named? | WP-56's `reent-tls-bringup` road-to-green row, item 1. Run 2026-09-01: no window collision — the stub links at `0x100400000`, not the `0x400000` window it reserves. The fault is the crt0 startup crossing: `_cygwin_crt0_common` calls `cygwin_internal` Microsoft-style into the faced runtime's System V veneer, and faults before `main`. Interposing one local `cygwin_internal` (`-DBRIDGE`) reaches `main`; past it, one ordinary `printf` into the faced libc produces no output while control survives it, so the boundary stands at every host-to-faced-runtime call, not only startup's. So item 1 is crossing the Microsoft↔System V ABI boundary, not reconciling a window; a decision record carries the reframing and row 14's first account is superseded. |
Spike 1 was an afternoon and it decided a layer, against us. Spike 2 came back
yes and moved a question from whether to when. Spike 3 was the expensive one,
and a no there would have sent the program to the veneer-thunk fallback, which
is a different program; it came back yes, and left behind a measurement that
moves the red-zone question from the host onto our own layer. Spike 4 gated
nothing technically; it measured whether the whole edifice repairs what it was
built to repair, which is the question worth answering before anything large is
funded, and it came back yes. Spike 5 gated nothing in the end either: the
triple was decided on 2026-08-29 without waiting for it, and the count it
produced the same day is the size of the patch set that decision commits to.
One package, well inside the threshold DR-0001 set in advance. Spike 6 was the
follow-on spike 1 forced: with `%fs` gone something had to carry the thread
pointer, so it measured four `%gs` carriers against spike 1's own cases, found
three that hold, and let DR-0003 choose on ownership rather than on persistence.
Spike 7 is the follow-on spike 3 forced, and it ran on 2026-08-29: spike 3 caught
our own delivery destroying the red zone, and spike 7 asked whether delivery can
be made to reserve it. It can. A frame built 128 below the interrupted `%rsp`
left the red zone whole across every delivery, the handler still ran and
returned, and a value carried only in the red zone came back intact on the fa
side. That does not retire the `-mno-red-zone` flag on its own -- the flag stands
as policy either way, and what a reserved delivery costs in Cygwin's real
`sigdelayed` is unpriced -- but it establishes that an ELF-faithful repair at the
delivery site is available for WP-43 to weigh against the flag rather than
foreclosed.

## Spike 1, the thread pointe

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
spike's README reads it. What replaces `%fs` was reserved to the operator by
`AGENTS.md` and is now settled: a follow-on spike, `spike/gs-thread-pointer/`,
measured four `%gs` carriers the same day against these same cases, and DR-0003
took carrier C3 — a runtime-owned thread pointer kept below the stack base in
Cygwin's `_my_tls` shape and reached through `%gs`. Where the `%fs` base came
back zero on the first check of every descheduling case, the `%gs` carriers
returned their pointer across 17.6 billion checks with none, at about a sixth of
emulated TLS's per-access cost. That spike's README reads its own transcript.

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
measured here, and it should be measured before that package is written rathe
than discovered inside it. `spike/map-and-jump/results-2026-08-29.txt` is the
transcript and that spike's README reads it.

## Spike 3, the ABI crossing

Run 2026-08-29, and the answer is yes on the crossing and no on the red zone,
which are less related than they sound.

The crossing holds in both directions at one function's width. A System V
caller passed six integers, eight doubles and two stack arguments into a
`sysv_abi` entry point that then made five descents into Microsoft x64 --
`GetCurrentThreadId`, `VirtualQuery`, `QueryPerformanceCounter`, the runtime's
`snprintf`, `Sleep` -- and returned with every System V callee-saved registe
intact. Going the other way, a Microsoft caller reached System V code with all
eight callee-saved GPRs and all ten callee-saved XMM registers intact, which is
the direction that could leak, because `%rsi`, `%rdi` and `%xmm6` through
`%xmm15` are callee-saved to a Windows caller and volatile to a System V
callee. Windows called in four ways -- a thread start, an APC, a vectored
exception handler, a Cygwin signal handler -- and each reached System V code one
frame down. A null store in Microsoft code beneath a System V frame came back
as SIGSEGV and left by `siglongjmp` past that frame, which is the SEH claim
AGENTS.md forbids assuming, tested at the only width anyone has tested it.

The red zone is destroyed, and the finding is which layer destroys it. Windows
does not: preemption with a burner on every processor moved nothing, two
thousand suspend-and-restore hijacks moved nothing, and Windows' own exception
dispatch left the nearest 320 bytes below `%rsp` untouched, which is well
outside the 128 the psABI reserves. Cygwin's signal delivery takes the word at
`%rsp-8` on every one of two thousand deliveries and everything down to the
1024 bytes watched, because it hijacks the thread and builds the handler's call
frame at the interrupted stack pointer.

So `-mno-red-zone` throughout stands, and it is not free: gcc gives a
`sysv_abi` leaf a red zone on this target and the flag turns `-32(%rsp)` into
`subq $32, %rsp`. But the code breaking the guarantee is Cygwin's own delivery
path, which this project already intends to modify, so there is a second option
beside the flag and choosing between them is the operator's. `AGENTS.md` records
it as open.

`spike/abi-crossing/results-2026-08-29.txt` is the transcript and that spike's
README reads it, along with what a one-function measurement does not reach:
unwind data, `DllMain`, and the runtime actually rebuilt.

Re-verified 2026-08-31 on the primary root, after the environment moved
(DR-0038) and the verdict briefly read `no` there: the flip was gcc 14 eliding
the null-store specimen, not the host, and with the specimen repaired every
crossing case passes under 3.6.10 too. `results-2026-08-31.txt` is that
transcript. It also reads the red zone differently, because the Cygwin unde
it is different: 3.6.10's stock delivery leaves the reserved 128 bytes alone
where 3.0.7 took `%rsp-8`. The paragraphs above describe 3.0.7; DR-0006 was
decided on that measurement, and reading the new one against it is the
operator's. The spike README's re-verification section carries the detail.

## Spike 4, the payoff

Run 2026-08-29, and the answer is yes. This is the spike that asks whethe
building the thing repairs what it was built to repair, and the sharpest form
of that question is a string: does rpm write `libc.so.6(GLIBC_2.2.5)(64bit)`
when it is pointed at a library we made up.

It does, and not merely in the right shape. The line a synthesized `libc.so.6`
carrying one verdef node yields from el8's own `elfdeps` is byte-identical to
the line el8's `libc-2.28.so` yields from the same binary, and to the
requirement a synthesized consumer emits from its `.gnu.version_r`. The edge
closes: what the library provides is what a program asks for, spelled the same
way at both ends, which is the whole of what `doc/symbol-versioning-formats.md`
says PE can never do.

Synthesized again with el8 libc's whole node list -- 29 of them, `GLIBC_2.2.5`
through `GLIBC_2.28` and `GLIBC_PRIVATE` -- the provides set is identical to
the vendor's, thirty lines each. So one node proves the mechanism and the
ladder prices the veneer: a package requiring `GLIBC_2.14` is not satisfied by
a library defining only `GLIBC_2.2.5`, and `rpm`'s own binary needs three of
the nodes.

One finding is a trap worth carrying forward. A versioned provide is read off
the base verdef node, not off `DT_SONAME`; a library that gets those two
strings out of step provides under one name, is required under the other, and
says nothing about it. In a library a linker produced they are always the same
and the question never arises, which is exactly why a synthesized one will get
it wrong. Measured here, by a fixture built to get it wrong on purpose.

What the verdict does not buy is automatic generation on the build host. That
needs an rpm carrying `elfdeps` and `fileattrs`, and Cygwin's ships neither, so
the stage 0.5 admission survives -- changed from a format impossibility into an
installation gap. `spike/versioned-libc/results-2026-08-29.txt` is the
transcript and that spike's README reads it.

## Spike 5, the target triple

The triple has four fields and they are not equally load-bearing. The fields
are `cpu-vendor-kernel-os` in `config.sub`'s own naming, none of them called
`abi`; only the kernel and the libc are consulted by the machinery that would
break, and `vendor` is passed through untouched and read by almost nothing. So
the triple is `x86_64-elfsysvnt-linux-gnu`, decided on that reasoning and
recorded in DR-0001: it puts the honest name where it costs least and leaves
`linux-gnu` standing where configure actually looks. Neither of the two
load-bearing fields is thereby a lie, which DR-0005 settles and
`doc/target-definition.md` states: `gnu` is glibc exactly, and `linux` is the
Linux kernel ABI bounded at raw syscall dispatch, which this project satisfies
by rebuild instead.

Buildroot has shipped `x86_64-buildroot-linux-gnu` on the same grounds for ove
a decade, and crosstool-NG ships `unknown` in that slot, so neither the shape
nor the length is novel.

What the vendor costs is the open question. A package that matches `*-linux-gnu`
is unaffected; one that matches the literal `*-pc-linux-gnu` o
`*-unknown-linux-gnu` misses, silently, and takes a configure branch nobody
intended. Such packages exist. How many is the size of the patch set the
decision commits to, and guessing at it is precisely the habit these spikes
exist to break.

The script takes the `config.sub` shipped by each package in the el8 source
set, feeds it three candidates, and records the canonicalized output of each:
the masquerade `x86_64-pc-linux-gnu`, the vendor-honest
`x86_64-elfsysvnt-linux-gnu`, and the kernel-honest `x86_64-pc-elfsysvnt` as a
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

## Spike 6, the %gs thread pointe

Run 2026-08-29, and the answer is yes. Spike 1 took `%fs` away that afternoon
and left the thread pointer without a carrier, so this measured four: a fixed
`TlsSlots` index, the word below the stack base in Cygwin's `_my_tls` shape,
`NtTib.ArbitraryUserPointer`, and the PE TLS directory. Three held identically
across spike 1's twelve persistence cases -- 17.6 billion reads with none lost,
through 45 million real context switches -- and each read a glibc-shaped block
back correctly. Persistence did not choose between them; ownership and precedent
did, and DR-0003 took carrier C3, the word below the stack base that Cygwin
already keeps by this chain and the runtime owns. The access costs one extra
load over a global, roughly a sixth of what emulated TLS costs, which is the
comparison that matters once spike 1 has removed the native one.
`spike/gs-thread-pointer/results-2026-08-29.txt` is the transcript and that
spike's README reads it.

## Spike 7, reserving the red zone

Run 2026-08-29, and the answer is yes. Spike 3 found the red zone destroyed and
named the layer: Cygwin's own signal delivery builds the handler's frame at the
interrupted stack pointer and takes `%rsp-8` first, where Windows itself leaves
the nearest 320 bytes alone. That left `-mno-red-zone` standing as policy and one
question beneath it, whether delivery could instead reserve the 128 bytes before
it builds, the way a Linux kernel does, and let the flag come off. This spike
asked exactly that, without rebuilding `cygwin1.dll`: spike 3 already built the
thread-hijack delivery it measured against, and this put a handler frame back on
top of that hijack two ways -- at the interrupted `%rsp`, which had to clobber,
and 128 below it, which must not -- and watched the red zone under each. The
naive frame lost the word at offset 8 on every delivery, reproducing spike 3's
finding and proving the model destroys the red zone where the real path does. The
reserved frame left the 128 whole, nearest write at offset 136, on every
delivery; the handler ran and returned each time; and a value carried only in the
first red-zone word came back intact on the far side, while the same value broke
under a naive delivery. It is a model of delivery rather than the real
`sigdelayed`, in the way `spike/gs-thread-pointer/` measured a stand-in fo
`_my_tls`, and WP-43 re-measures the real path and prices what reserving costs
there. What it gates is narrow: the yes lets a delivery-site repair that honors
the red zone for compiled and hand-written code alike be weighed against the
flag, rather than foreclosed; it does not retire the flag, which stands as policy
until WP-43's record. By the discipline DR-0001 and DR-0003 followed that record
is taken against this spike's transcript rather than ahead of it.
`spike/redzone-delivery/README.md` carries the mechanism, the cases and the
reading; `results-2026-08-29.txt` is the transcript.

## Spike 8, reading a zeroed `%fs` base

Run 2026-08-29, and the answer is that it faults and a handler resumes from it.
Spike 1 measured the base and found it zero after anything that deschedules the
thread; it never measured the next instruction, and that gap turns out to
decide something larger than its size suggests.

Vendor binaries are built for real Linux and reach for TLS through `%fs`, and
no toolchain choice of ours touches a binary that arrives already linked. The
operator's direction is that load-time rewriting is the fallback for that case,
which puts the weight on finding every access site rather than on rewriting
one: in a linked executable the local-exec relocations have been consumed, so
a rewriter is reduced to scanning bytes, and code-versus-data on x86-64 has no
sound general answer. It will miss sites. What a miss costs is what this asks.

If an access through the zeroed base faults, and a vectored handler can
identify the instruction, emulate it through the C3 carrier and resume, then
the rewriter is an optimization over a sound fallback and is allowed to be a
heuristic. If it reads something instead, the rewriter has to be exhaustive,
nothing here can make it so, and the fallback narrows to binaries we built
ourselves — which is barely a fallback, since a binary we built is one we
could have compiled correctly.

It takes nine of spike 1's twelve cases unchanged, the spinning one included,
since that is the one with no call site to hook, and its own README says where
the two lists part. Ten of its twelve events lose the base and an access
afterwards raised an access violation every time; the other two are the two
descheduling cases spike 1 also recorded as surviving, and nothing read through
a zeroed base anywhere in the run.

The finding the fallback rests on is what Windows hands the handler. With the
base at zero the effective address is the offset, so the faulting address is
the TLS displacement itself and the handler never computes an address: `0x0`,
`0x40`, `-0x8` and `-0x18` were each reported as themselves. What is left is a
length and a destination register. Nine forms were decoded, emulated through
DR-0003's carrier C3, and resumed correctly, each checked both against the
value the block held and against a landing pad the probe computed beside the
access; the interrupted code got its other registers and its carry flag back
intact. The case that decides it is the one with no call site, and it held:
1,304,000 reads under a burner on every processor, every one a fault, every one
correct, and 15,662,000 more across 24 threads with none wrong.

So the rewriter may be a heuristic, and that is the verdict — over the
data-movement forms. The qualifier is the spike's second deliverable and it is
not small: a read-modify-write access, `addl $1, %fs:-0x4` and its relatives,
is refused by name rather than guessed at, because emulating it means emulating
`EFLAGS`. A missed site of that shape is a `SIGSEGV` rather than a slow
success. Closing that gap is a choice between emulating the flags and requiring
the rewriter to be exhaustive over exactly the forms it is least able to be
exhaustive about, and what it costs turns on a census nobody has run. A handled
fault costs about 2.2 microseconds against half a nanosecond rewritten — three
orders of magnitude, which is fine for a miss and would be ruinous as a policy.

`spike/fs-base-fault/README.md` carries the method, the reading, and what the
measurement does not reach; `results-2026-08-29.txt` is the transcript. The
verdict goes to `doc/proposals/0003-vendor-binary-tls-rewriting.md`, which was
written against it rather than ahead of it.

Nothing in phase 1 waited on this. What waits is the loader's answer to a
vendor binary, which is phase 3 at the earliest, and the rewriter is not yet
cut into a work package.

## After the spikes

No package waits on a spike. All eight have run and the recommended path
survived the one that could have taken it away; the seventh gated only whethe
a delivery-site red-zone repair is available to weigh against `-mno-red-zone`,
and the eighth gated only what a load-time rewriter for vendor binaries is
allowed to be, which is phase 3's question at the earliest. Nothing starting
now depends on either.
`ROADMAP.md` inventories the work and `IMPLEMENTATION-PLAN.md` cuts it into
packages; the sketch below is the shape both of them fill in.

The target triple wanted deciding before the first package was built, and it
was, on 2026-08-29. The TLS model wanted deciding on the same footing, and it
was, the same day: spike 1 came back no that afternoon, the gs-thread-pointe
spike measured the replacements, and the operator settled DR-0003 on carrie
C3 — a runtime-owned thread pointer through `%gs`. The toolchain can now be
configured around a named thread pointer. The toolchain follows, which is
otherwise routine
cross-toolchain work. Then the loader, where musl's `dynlink.c` is the working
model and the verdef and verneed matcher is the part musl leaves out.
`elfsysv1.dll` and the libc veneer come after the loader can run something. The
`r_debug` rendezvous can start as soon as there is a loader to announce
objects, and it should, because the alternative is debugging a world Windows
tools cannot see.
