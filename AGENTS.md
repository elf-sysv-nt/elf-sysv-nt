# AGENTS.md

A userland kernel that presents ELF and the System V AMD64 ABI to everything
above it, and Windows NT to everything below. Derived from Cygwin, which
already funnels every host call through one DLL; this project re-faces that DLL
rather than replacing what is behind it. The point is that a Linux userland
builds against it with its object format, its symbol versioning, and its
loader semantics intact.

Read `doc/elf-technical-breakdown.md` for the design and `doc/milestones.md`
for the order of work. What has been built is tracked in
`doc/status/delivered.txt` and reported by `bin/build_status.py`, which
`bin/refresh-next-steps.py` renders as the `Next-Steps.md` dashboard; both
documents here are the plan rather than the record of progress, and may be
wrong about anything not yet built. `doc/ROADMAP.md` inventories what has to be
written once the spikes have answered, and `doc/IMPLEMENTATION-PLAN.md` cuts
that inventory into work packages; both are written along the recommended path
and name the branch where a spike could send the program elsewhere.

## Risks worth knowing before touching anything

Claims this design rests on that have never been measured are listed in the Not
verified section of `doc/elf-technical-breakdown.md`. One of the two
load-bearing ones has since been settled and it went the wrong way: Windows
does not preserve a user-written FS base, so `%fs`-relative TLS is off the
table and nothing may assume it. The other went our way on the same day: spike
3 crossed the ABI boundary in both directions, through Windows callbacks and
through a fault, at one function's width. That narrows the claim rather than
settling it, and what it does not reach is listed in that spike's README --
unwind data, `DllMain`, and Cygwin's source rebuilt rather than called. Do not
read the spike as licence to assume the whole runtime survives re-facing.

The red zone is ours to break, not Windows'. Spike 3 measured the host leaving
the reserved 128 bytes alone under preemption, thread hijacking and its own
exception dispatch, and Cygwin's signal delivery taking `%rsp-8` on every
delivery. The delivery path is now repaired: DR-0006 settled the direction and
WP-43 built it, DR-0030 recording the shape, so delivery reserves the 128 bytes
before it builds the handler frame. `-mno-red-zone` throughout stays the compile
policy, the scaffold DR-0006 named, carried until the repair covers every
package.

Cygwin binaries are backward compatible only. Nothing built against a newer
`cygwin1.dll` runs on an older one, so borrowing a binary from a newer tree is
never an option and building from source is the only route.

Never run one root's binaries from another root's shell. A binary resolves
`cygwin1.dll` off the invoking shell's PATH, and the failure is a hang rather
than an error, so it reads as a slow tool instead of a mistake.

Self-mapped anonymous executable memory is malware-shaped. Enterprise endpoint
protection will object to the loader, permanently, and that is a deployment
constraint rather than a bug to fix.

## Conventions

Design first, and surgically. When you change something, update the governing
design or decision document in the same change, first or alongside, never
after. This grounds the change and stops unrelated rewrites.

Verify against real source at a known ref. Anchor a claim to
`git show <ref>:path` rather than to memory, and confirm the tag or commit
before asserting behavior. A measurement and a recollection are different
things and the document should say which one it is carrying. Every document
here ends with a Not verified section for exactly that reason; keep it current
rather than letting it rot into a list of things that were checked years ago.

Ask what Linux and GNU already have before writing anything. This is the first
question on every piece of code, not a fallback when the work looks large: does
an implementation exist upstream, and can it be used as it stands, adapted, or
at minimum read as the specification? Most of this format was designed by the
people who wrote that code, and a routine reimplemented from a document is a
second reading of the same specification, with a second set of mistakes. Say in
the governing document which of the three happened — used, adapted, or written
from the specification — and when it is the third, say what made the first two
unavailable. "It seemed easier" is not one of the reasons.

Ask what the layer beneath already has, before writing a body or declaring a
stub. This is the same question pointed inward, and it is the one the veneer
lives on: most of what a symbol needs is usually already in the runtime under a
different name or a different shape, and a body composed from what is there
beats a body written beside it.

Ask it against implementations, not signatures. The names on this seam are
glibc's and the bodies are Cygwin's, and they agree on shape far more often
than they agree on behaviour. `__vsnprintf_chk` has exactly the signature to
compose a fortified printf from, and its body checks the buffer bound and
ignores the fortify level entirely; a composition that type-checks can still be
wrong, and only reading the body says which.

Name what a composition changes. Building a stream call out of a buffer call
alters when bytes appear, what a partial write leaves behind, when an error is
observable, what the lock covers, whether anything allocates, and what a signal
arriving mid-call sees. A composition whose delta is nil is a filled stub and
DR-0052 governs it. One with a delta is recorded with the delta named, or it is
refused. An unrecorded delta is the failure this rule exists to prevent.

Composition inherits the floor. Anything built from the runtime is at most as
strong as the runtime, so a body cannot compose its way above the platform
beneath it — `__fprintf_chk` cannot acquire a `%n` check that no Cygwin `_chk`
body performs. Being stronger than the floor means declining to compose and
implementing independently, which is a larger decision than writing a body and
belongs to the operator rather than to the symbol.

Licenses are checked before code is lifted, not after, and the check is
DR-0074's: the file's licence text, the FSF's compatibility guidance, and
recorded practice — a named project that has done the same combination in
public, at a ref somebody here read — written into every lift record as a
Precedent section in DR-0037's shape. It tends to come back permissive rather
than blocking. This tree is LGPLv3-or-later, so LGPL-2.1-or-later material —
glibc's, most of the GNU runtime's — can be taken outright, which is the
footing the vendored headers under `veneer/include/` already stand on. GPL
material is the one that turns a lift into a distribution obligation on the
whole: flinux, Qiling and QEMU's linux-user are in that class; Blink is not,
being ISC, so its Linux syscall layer sits in the permissive class beside
musl, which is MIT and the loader's working model. What usually rules
upstream code out here is coupling rather than licence — glibc's resolver
assumes `_rtld_global`, its own `link_map`, and being the process's first
mover — so name the coupling when that is the reason, because "it is GPL" and
"it assumes a kernel we do not have" are different claims and only one of them
was ever true of glibc.

This tree is LGPLv3 or later, which DR-0004 records as inherited rather than
chosen: `elfsysv1.dll` is Cygwin's `winsup` re-faced, Cygwin's exception
excludes a library based on the Cygwin library by its own definition, and the
licence follows the derivation. The linking exception carries forward with
the modified library — DR-0037 records the reading and the precedent, MSYS2
and Git for Windows being the practice it rests on — which is what lets el8's
GPLv2-only software link an LGPLv3 runtime. Do not invent licence text: the
exception is stated in `doc/licensing.md` by reproducing upstream's wording
verbatim, and it stays that way.

Every installer and configurator is idempotent. Running it twice, or ten times,
leaves the same result as running it once, whatever state the last run left
behind. Derived configuration is reseeded from a pristine template each run and
then has the managed settings reapplied, never edited in place forever. When
the tool stops managing something it once managed, it removes the orphan rather
than merely ceasing to touch it.

Commits are conventional: `type(scope): summary`, imperative, scoped to one
logical phase, no `Co-Authored-By` trailer. Most carry a subject line and
nothing else; a body appears when the reasoning is not recoverable from the
diff. Documents and source land at mode 644.

Push on judgment rather than on a cadence, and on your own judgment: push when
the work is at a point worth publishing rather than asking whether to. A spike
that has reached its verdict with its transcript and its tests is such a point.
Published history is append-only, so no force-push and no rebasing a commit
that has already left the machine. When a push fails, stop and say so rather
than carrying on as though it succeeded.

Commands written for this project follow docopt, with a `Usage:` block as the
parsing source of truth. Every setting reachable by environment variable also
has a command-line option, and precedence runs option, environment, config
file, built-in default.

## Layout

`doc/` is tracked and holds the governing documents. Working notes — session
handoffs, surveys, and anything true only of one machine — live in the
untracked working area, are authoritative for nobody else, and are never cited
from a tracked file, since a reader who clones this cannot open them.

`spike/<question>/` is tracked and holds the evidence behind a decision in
`doc/`: the script that measured it, its sources, and the transcript the script
produced, named by the date it was run. A spike is kept so that a finding can
be re-measured rather than believed. Rerunning the script has to regenerate the
transcript, so a spike whose script no longer runs is a defect in the same way
a failing test is.

The first five spikes in `doc/milestones.md` are the gates that stood before the
reserved decisions; all five ran on 2026-08-29 and have their verdicts. Six more
followed as the work and then the design-gaps review surfaced them —
`spike/gs-thread-pointer/` the sixth, a follow-on to spike 1's no that measured
the `%gs` carriers replacing the refuted `%fs` base and fed DR-0003 — and
`doc/milestones.md` now records all eleven, each kept on the same terms.

`doc/decisions/` holds one settlement per file with an index beside them, and
`doc/proposals/` holds the change that produced each. A decision record is
append-only: reversing one means a new record pointing back, never an edit to
the old.

A decision record an implementing agent takes carries `Status: provisional`
until the operator ratifies it. Ratification is cheap by design: one sweep
record settles many, and reopening any of them is the same new-record
mechanism. A record taken through `doc/decision-ladder.md` names the tier that
discriminated, which is the line a ratification pass reads first; where the
ladder reaches tier 8 without deciding, the entry parks with its survivors
named rather than proceeding on a guess. The reserved decisions and the
operator's own records are not provisional; the sweep in DR-0036 settled the
agent records taken before the convention existed.

A certification run against a substitute for the thing it certifies — a newer
glibc standing in for el8's 2.28, a WSL userland for a real el8 one — is
permitted and creates a row in `doc/substitutions.md`: what was substituted for
what, where, and what burns it down. The row closes when the certification
reruns against the real target and matches, or when its divergence is written
down as justified. A certification that hides its substitution rather than
recording it is the failure this rule exists to prevent.

## Where autonomy stops

Run a spike through to its stated verdict without asking permission along the
way. Then stop, at the spike boundary, and report the answer rather than
beginning the work it implies.

Three points are decisions rather than tasks, and an agent must not settle them
alone.

The target triple. Settled on 2026-08-29 as `x86_64-elfsysvnt-linux-gnu`, by
the operator, and recorded in `doc/decisions/0001-target-triple.md`. That
record also carries the share of affected packages at which it should be
reopened, which is a decision for the operator too. An agent reading spike 5's
verdict reports it against those bands and stops there.

DR-0005 closes the adjacent question, and an agent should read it before
proposing anything about the triple. The fields are `cpu-vendor-kernel-os`,
`gnu` is glibc without qualification, and `linux` is a claim this project means
everywhere except raw syscall dispatch. Neither load-bearing field is a lie, so
"the triple is dishonest" is not an opening for a reopen; the measured price of
substituting either is in that record and in
`doc/proposals/0004-the-bounded-linux-claim.md`.

The TLS model. Spike 1 ran on 2026-08-29 and the answer was no: a user-written
FS base does not survive a context switch, or even a preemption, on this
Windows. That took `%fs`-relative TLS away and changed the toolchain layer
rather than merely adding work to it. The replacement was measured by
`spike/gs-thread-pointer/` the same day and settled by the operator in
`doc/decisions/0003-tls-model.md`: a runtime-owned thread pointer through `%gs`,
carrier C3, the shape Cygwin's `_my_tls` already uses. WP-30's body may now be
written against that model. The one carried risk is the operator's to reopen,
not an agent's: DR-0003 records that the spike measured a stand-in, and WP-2x
re-measures the real `_my_tls`; if it diverges, the reopen is a new record
pointing back at DR-0003.

The ABI boundary. Spike 3's answer decided whether the runtime is rebuilt
System V-faced at all, and on 2026-08-29 it came back yes, so the veneer-thunk
fallback stays where it is. The decision that opened in its place is the red
zone: the host respects the reserved 128 bytes and Cygwin's own signal delivery
does not, which makes `-mno-red-zone` throughout one repair and a 128-byte gap
in the delivery path another. Which of the two is the destination was settled
on 2026-08-29 in `doc/decisions/0006-red-zone-direction.md`: the delivery site
is repaired and the flag is scaffolding carried until it is. What that record
does not settle is the price, which WP-43 measures against Cygwin's real
`sigdelayed` rather than against spike 7's model, and which DR-0006 reads
against bands written before the number exists.

The flag is the standing policy meanwhile. It costs a stack adjustment in every
leaf and does not reach hand-written assembly at all, which is the residue
WP-16's ledger exists to bound; the delivery repair costs a patch to code this
project already means to modify and buys the psABI guarantee back for compiled
and hand-written code at once. That is the reasoning DR-0006 records. What is
still open is only the number, so an agent may take the measurement and must
not read the verdict off it.

## Testing

Code gets tests. The loader, the relocator, and the verdef and verneed matcher
parse attacker-shaped input from the first line they run, so they get unit
tests over recorded fixtures and a fuzz target fed malformed and truncated
ELF. Write these alongside the implementation. A relocator that has never seen
a truncated `PT_DYNAMIC` is not finished.

Spikes get transcripts. A spike is correct when rerunning its script
regenerates its recorded result on the same machine, and a spike whose script
has rotted is a failing test.

The runtime gets differential tests against Linux where a Linux answer exists.
TLS layout, auxv contents, `r_debug` shape, and symbol resolution order are all
specified, and the specification is checkable against a real glibc rather than
against our own reading of the document.

Build leaf to trunk. Nothing depends on functionality whose tests have not been
written and have not passed.
