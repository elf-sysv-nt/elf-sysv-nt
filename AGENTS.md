# AGENTS.md

A Linux kernel personality for Windows NT, emulated at the `syscall` boundary:
the x86-64 Linux syscall table as kernel 4.18 had it, presented to an el8
(Rocky Linux 8) userland, over a core written once and two substrates that run
user code. Substrate N runs it as NT threads in an ntdll-only host process,
reached through a gate instead of a `syscall` trap, and needs the userland
rebuilt; substrate H runs it on Windows Hypervisor Platform vCPUs in one
kernel process per user and runs el8's shipped binaries unmodified. Nothing
here derives from Cygwin; what was borrowed from it and from WSL is ideas and
on-disk formats, listed in proposal 0011 § 17.

Read `doc/design/Architecture.md` first: it is the map over the design of
record, current at every commit, and points into proposals 0011 and 0012 for
detail. `doc/design/Substrate-Interface.md` is the contract between the core
and a substrate; `doc/design/Substrate-N.md`, `doc/design/Core-Phase1.md`
and `doc/design/Core-Phase2.md` record what is built. `doc/milestones.md` holds the spikes and their verdicts.
`doc/design/proposals/0011-a-kernel-at-the-syscall-boundary.md` is the design
itself, long and accepted, and 0012 completes it where it spoke for one
substrate only.

What is built is small and this document says so rather than implying
otherwise: substrate N is certified against its conformance bar, and the
core's Phase 1 runs a static program that calls `write` and `exit_group`
through the gate. Everything else is designed and unbuilt.

## Risks worth knowing before touching anything

The spikes are one host, one Windows build, one AMD processor. Every number
under `spike/` is an existence proof; none is a portability claim. The floor
the design states (Windows 10 1809 for function, Windows 11 for
certification) is derived from when each API arrived, not from a run on each.

A sentence about a mechanism is wrong about one substrate unless it names
which. The thread pointer is `%gs` under N and `%fs` under H; `fork` is an NT
process clone under N and a page-table copy in one process under H; the host
is ntdll-only under N and Win32 under H. Governing prose names N, names H, or
says both answer alike. The rule exists because a rewrite of
`Architecture.md` from records taken before H existed produced four
unqualified sentences that were true of N and inverted for H, and every
checker was green over them; a person reading one sentence caught it.

Windows does not preserve a user-written FS base across a context switch
(spike 1). `%fs`-relative TLS is unavailable under N and nothing may assume
it; N's userland reaches its thread pointer through `%gs`.

The red zone is kept whole on both delivery paths, and both were measured:
the asynchronous hijack builds its frame below the 128 bytes (spike 38), and
NT's own dispatch of a synchronous fault places its records 568 bytes and
more below the interrupted `%rsp` (spike 47). DR-0050 stands; the compiler
defaults to the red zone and nothing may assume a leaf that avoids it.

A process may hold one mapped WHP partition at a time (spike 43). Substrate
H's shape follows from that fact and from nothing else; do not design a
partition per Linux process inside one host.

The kernel never hands a host call a user address. Under N a lazily committed
user buffer fails the I/O manager's probe; under H no host call can take a
guest address. Every I/O goes through `user_copy_*` and a kernel buffer.

The design stands on undocumented NT interfaces (`RtlCloneUserProcess`, AFD,
placeholder replacement of section views, `FileStatLxInformation`, the TEB's
`TlsSlots`) and on WHP behaviour Microsoft does not document (lazy mapping,
one mapped partition per process). Each gets a presence probe in the gate
tier so that a Windows update reads as a red check rather than a hang.

Self-mapped anonymous executable memory is malware-shaped, and so is a process
that clones itself and rewrites its own threads' contexts. Substrate N does
the second; endpoint protection may object, and that is a deployment
constraint recorded per host rather than a bug. Substrate H does neither.

## Conventions

Design first, and surgically. When you change something, update the governing
design or decision document in the same change, first or alongside, never
after. This grounds the change and stops unrelated rewrites.

Verify against real source at a known ref. Anchor a claim to
`git show <ref>:path` rather than to memory, and confirm the tag or commit
before asserting behaviour. A measurement and a recollection are different
things and the document should say which one it is carrying. Every document
here ends with a Not verified section for exactly that reason; keep it current
rather than letting it rot into a list of things that were checked once.

Measure a fact before escalating it. A fork that turns on how the host or the
toolchain behaves is a fact, and a fact is measured with a spike, every
surviving candidate, before any decision reads it. A fork that is a value
goes to the operator. `doc/design/decision-ladder.md` is the ladder; a record
taken through it names the tier that discriminated.

Ask what Linux and GNU already have before writing anything. This is the
first question on every piece of code: does an implementation exist upstream,
and can it be used as it stands, adapted, or at minimum read as the
specification? Say in the governing document which of the three happened, and
when it is the third, what made the first two unavailable. "It seemed easier"
is not one of the reasons. glibc is used unmodified above the kernel except
for its `sysdeps`; Linux's man pages, LTP and the UAPI headers are the
specification; `binfmt_elf`, `n_tty`, `fs/namei.c` are behaviour to match, not
code to copy.

Licences are checked before code is lifted, not after, and the check is
DR-0074's: the file's licence text, the FSF's compatibility guidance, and
recorded practice, written into every lift record as a Precedent section.
This tree is LGPL-2.1-or-later by choice (DR-0095), glibc's licence, so
LGPL-2.1-or-later material can be taken outright and GPL material (LTP, the
Linux sources) is read as specification and never lifted; where a body's shape
is taken from a GPL file rather than from a man page, the record says so and
the body is rewritten from the page. `doc/design/licensing.md` states the
position in a page.

Every installer and configurator is idempotent. Running it twice leaves the
same result as running it once, whatever state the last run left behind;
derived configuration is reseeded from a template each run; when the tool
stops managing something it once managed, it removes the orphan.

Commits are conventional: `type(scope): summary`, imperative, scoped to one
logical phase, no `Co-Authored-By` trailer. Most carry a subject line and
nothing else; a body appears when the reasoning is not recoverable from the
diff. Documents and source land at mode 644.

Sessions work in a worktree cut from the trunk (`bin/session-start`), never
in the shared checkout, and land through `bin/session-land`, which merges to
`march` and fast-forwards `main`. Push on judgment rather than on a cadence;
published history is append-only, so no force-push and no rebasing a commit
that has left the machine. When a push fails, stop and say so.

Commands written for this project follow docopt, with a `Usage:` block as the
parsing source of truth. Every setting reachable by environment variable also
has a command-line option, and precedence runs option, environment, config
file, built-in default.

## Layout

`core/` is the kernel above the substrate line: the gate, the syscall table,
`binfmt_elf`, the VMA tree, the vDSO, the VFS with its descriptor layer and
in-kernel file systems, and the host-glue files that speak to NT directly
and say so (`host.c` line by line, `hostfs_nt.c` with `substrate-line:
below` in its head). `bin/check-substrate-line` walks it on every gate run
and refuses `Nt*`, `WHv*`, `CONTEXT`, an NT `HANDLE` type, a Win32
named-object or port call, or a raw user pointer dereference. `test/core/`
holds the freestanding programs the criteria run and the shim that builds
each once against the gate and once against `syscall` for the oracle;
`test/t/` holds the criterion harnesses. `substrate/` is
the interface in C, the mock, substrate N, and the conformance suite that
certifies both. `seam/` is the process seam: the interface the core calls for
what crosses Linux processes, with its cross-process realisation for N and its
in-process one for H, both unbuilt and both outside the checker's walk. `test/` holds the suites
registry, the spike-regeneration runner and the core's oracle programs;
`toolchain/` the cross toolchain's patches and the rpm macros for N's rebuilt
userland; `acceptance/` the package-level framing that rebuild inherits.

`doc/` is tracked and splits three ways by what a file claims about the
present. `doc/design/` holds everything that states what the system is or how
a change to it is settled: the governed set, the layer documents, the decision
records and the proposals behind them. `doc/history/` holds what was true and
is kept as reasoning rather than as a plan (the founding surveys, the veneer
arc's retirement, the veneer's address-space protocol), and nothing there is
authority for present behaviour. `doc/` itself keeps the plans and the
ledgers, which are neither: `doc/ROADMAP.md`, `doc/milestones.md`,
`doc/deferred-work.md` and `doc/status/`. A document whose claim changes class
moves, and the citations move with it in the same commit.

Working notes (session handoffs, reviews of one commit, anything true only of
this checkout) live under `a/`, untracked, are authoritative for nobody else,
and are never cited from a tracked file, since a reader who clones this cannot
open them; `bin/check-worknote-refs` enforces that. A working note worth
keeping lands in `doc/history/`, made to stand alone. Session worktrees live
under `a/wt/`.

`spike/<question>/` is tracked and holds the evidence behind a decision: the
script that measured it, its sources, and the transcript the script produced,
named by the date it was run. A spike reproduces its findings, not its
measurements: verdicts and case words come back identical, numbers move.
`test/spike-regen.tsv` registers every spike and `test/t3-regen.sh` reruns
them; a spike whose script no longer regenerates its transcript is a defect
the way a failing test is. `doc/milestones.md` records all 47 with their
verdicts.

`doc/design/decisions/` holds one settlement per file with an index beside
them, and `doc/design/proposals/` holds the change that produced each. A
decision record is append-only: reversing one means a new record pointing
back, never an edit to the old. A record an implementing agent takes carries
`Status: provisional` until the operator ratifies it; a record taken through
`doc/design/decision-ladder.md` names the tier that discriminated, and where
the ladder reaches tier 8 without deciding, the entry parks with its survivors
named rather than proceeding on a guess.

A governed set states what the system is, in the present tense, current at
every commit: `doc/design/Requirements.md`, `doc/design/Architecture.md`,
`doc/design/Verification-Plan.md`, `doc/design/target-definition.md`,
`doc/design/licensing.md`, `doc/design/test-environment.md` and this file.
`doc/design/Substrate-Interface.md`, `doc/design/Substrate-N.md` and
`doc/design/Core-Phase1.md` carry the layers that are built. The bar on all of
them is that a reader implementing a layer never has to open a record to find
a value, a constant, a limit or a rule.

Citation runs both ways across the seam between governed prose and records. A
governed section that a record settled ends in one line of its own, the last
in the section:

    Settled by: DR-0024, DR-0063, DR-0101.

Every record filed from DR-0075 onward carries an `Amends:` header naming the
one section it changes most, so that the author names the prose that has to
move at the moment of writing. `bin/check-design-links` holds all of it as a
property of the tree: every in-force record cited somewhere, every citation
naming a record that exists and has not been replaced, every new record's
Amends resolving to a heading that exists. Supersession lives in the index's
Status cell as `; superseded by NNNN`.

A certification run against a substitute for the thing it certifies (a WSL
userland for a real el8 one) is permitted and creates a row in
`doc/design/substitutions.md`; the row closes when the certification reruns
against the real target.

Settled by: DR-0031, DR-0036, DR-0039, DR-0070, DR-0075, DR-0076, DR-0078.

## Where autonomy stops

Run a spike through to its stated verdict without asking permission along the
way. Then stop, at the spike boundary, and report the answer rather than
beginning the work it implies.

Some points are decisions rather than tasks, and an agent must not settle them
alone; the ladder's tier 8 is where they sit whatever the tiers above say.

The target triple, `x86_64-elfsysvnt-linux-gnu`, settled by the operator in
DR-0001 with DR-0005 fixing what the `linux` and `gnu` fields claim. Under this
design `linux` is true everywhere: the `syscall` instruction is never reached
under N and is the interface itself under H.

The TLS model under N. Settled by the operator twice: DR-0003 chose carrier
C3 for the veneer arc, and DR-0101 (2026-09-06) chose C1, `TlsSlots[63]`
reserved through the PEB bitmap, for this design, closing 0012's open
question 1. The carrier is a constant in `substrate_n.c` and the GCC patch;
reopening it is a new record, the operator's.

The substrate a deployment gets. Both are offered and the client's
environment decides (DR-0102): no hypervisor means N, stock el8 binaries mean
H, and the hypervisor is a prerequisite of H and never of the platform. Which
is built next is the operator's steer, recorded in `Core-Phase1.md` D0 and
confirmed 2026-09-05 as phase 2 on N.

What the project promises to verify. 0011's criteria are the criteria of
record as DR-0102 amends them; changing a criterion is a promise change and
the operator's, taken through a record.

## Testing

A new check is designated where it is written. Every suite and every checker
carries a row in `test/suites.tsv` saying what it is for and whether it gates:
`gate` for the fast, offline, deterministic ones the merge gate runs, `report`
for those needing a toolchain, a build product or a live el8, `standalone` for
the few that must never be automated. `bin/check-suites` refuses a check with
no row and holds the gate tier one-to-one with `ci/suites.txt`. There is
deliberately no default tier: a default is how a check stops being thought
about.

An absent input is reported, never failed. A suite that cannot run here
because a toolchain or a vendor dump is missing is not-checked rather than
broken, which is the distinction `test/t3-regen.sh` draws for spikes and the
one that keeps a green run from meaning two different things.

Code gets tests, and it gets them leaf to trunk: a substrate is certified
against the bar in `Substrate-Interface.md` before a line of the core rests on
it, and the core is certified against Rocky 8 before a package rests on it.
The loader, the VMA tree, the syscall dispatch and everything that parses an
ELF or a path see attacker-shaped input from the first line they run, so they
get unit tests over recorded fixtures and a fuzz target fed malformed input,
written alongside the implementation.

Spikes get transcripts. A spike is correct when rerunning its script
regenerates its recorded findings on the same machine.

The kernel gets differential tests against Linux where a Linux answer exists.
The auxiliary vector, `struct stat`, the signal frame, `/proc/self/maps`, and
the trace of a syscall sequence are all specified, and the specification is
checkable against Rocky 8 rather than against our own reading of the
document; `wsl -d rocky8` is the oracle Phase 1 used.

Settled by: DR-0084.
