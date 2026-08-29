# AGENTS.md

A userland kernel that presents ELF and the System V AMD64 ABI to everything
above it, and Windows NT to everything below. Derived from Cygwin, which
already funnels every host call through one DLL; this project re-faces that DLL
rather than replacing what is behind it. The point is that a Linux userland
builds against it with its object format, its symbol versioning, and its
loader semantics intact.

Read `doc/elf-technical-breakdown.md` for the design and `doc/milestones.md`
for the order of work. Nothing has been built, and both documents are
proposals that may be wrong. `doc/ROADMAP.md` inventories what has to be
written once the spikes have answered, and `doc/IMPLEMENTATION-PLAN.md` cuts
that inventory into work packages; both are written along the recommended path
and name the branch where a spike could send the program elsewhere.

## Risks worth knowing before touching anything

Four claims this design rests on have never been measured. They are listed in
the Not verified section of `doc/elf-technical-breakdown.md`, and the first two
are load-bearing: whether Windows preserves a user-written FS base across a
context switch, and whether the Cygwin runtime survives being rebuilt with a
System V export surface. Do not write code that assumes either answer before
its spike has run.

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

Licenses are checked before code is lifted, not after. Most of the prior art
this design leans on is GPL or LGPL: flinux, Blink, Qiling, glibc's resolver.
Reading them is fine. Linking them into a released image is a distribution
obligation, and the check precedes the lift.

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

Push on judgment rather than on a cadence. Published history is append-only, so
no force-push and no rebasing a commit that has already left the machine. When
a push fails, stop and say so rather than carrying on as though it succeeded.

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

The five spikes in `doc/milestones.md` are the current contents of that
directory, and all five are unrun. Spike 5 has its scripts and a sizing
measurement; what it has not got is a verdict.

`doc/decisions/` holds one settlement per file with an index beside them, and
`doc/proposals/` holds the change that produced each. A decision record is
append-only: reversing one means a new record pointing back, never an edit to
the old.

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

Spike 1's answer decides the TLS model, and a negative result changes the
toolchain layer rather than merely adding work to it. Report it; do not pick a
fallback model unilaterally.

Spike 3's answer decides whether the runtime is rebuilt System V-faced at all.
A negative falls back to the veneer-thunk design named in the breakdown, which
is a different program with a different cost, and that is a decision.

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
