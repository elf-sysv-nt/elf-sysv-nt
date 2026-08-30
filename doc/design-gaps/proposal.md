# Proposal — closing the design gaps

Status: raised
Date: 2026-08-30
Analysed against: `3069214` on `main`
Reads: `doc/design-gaps/findings.md`, whose eight findings this corrects, and
`doc/decision-ladder.md`, through which its open questions were resolved on
2026-08-30

Each finding gets a correction with an owner, an artifact, and a condition
that passes or fails. Where the correction is engineering it is cut into work
packages in the plan's own format, numbered in the phases they belong to, with
the gaps in the numbering meaning nothing as usual. Where it is a measurement
it is cut as a spike with its bands written before the number exists, the
DR-0001 discipline applied again. Where it is neither — the counsel question,
the ratification backlog — it is stated as a gate with a date, because those
two do not become safer by being restated as tasks.

## Context and scope

The review found the mechanical layers built to a high standard and the
deferred work unowned: the veneer's bodies, the winsup fork, the demand-side
numbers, and the counsel questions all sit outside the graph that claims to be
the critical path. Findings F1 through F5 are that gap in five aspects. F6 and
F7 are the institutional drift that let it go unnoticed, and F8 is a short
list of single-check items.

In scope: new work packages for the unowned engineering, new spikes for the
uncounted numbers, the classifier repair, the document repairs, two rule
changes to the conventions, and the licensing gate. Out of scope: any change
to a delivered artifact's behaviour beyond the classifier redo, any reopening
of the reserved decisions, and any renumbering. Records stay append-only
throughout; where a record cites a file that does not exist, the file is
created rather than the record edited.

## Goals and non-goals

Goals. Make the plan's graph contain the actual critical path. Put a number
under every claim the tail currently rests on. Repair the one concrete defect
found. Restore the same-change convention and give it a mechanical check so it
cannot rot silently again. Put the counsel questions on a clock.

Non-goals, each of which could reasonably have been a goal:

- Implementing any of the new packages here. This proposal prices and
  sequences; the packages carry their own delivery notes when they land.
- Auditing the delivered certifications by rerunning them. The substitutions
  ledger below makes the known gaps visible and WP-T2's environment burns
  them down; a wholesale re-audit is a different exercise with a different
  cost, and nothing found suggests it is needed.
- Settling the licensing questions. They are counsel's. What is settled here
  is only that they stop being unscheduled.

## F1 and F3 — the runtime fork and the veneer bodies become packages

These two findings are one gap seen from two ends: the veneer's bodies cannot
be wired until there is a real `elfsysv1.dll` for them to reach, and the fork
that produces it appears in DR-0000, DR-0007 and DR-0021 as a thing other work
defers to, never as a thing anything delivers. Four packages close it, two in
phase 2 and two in phase 5.

### WP-26 — winsup builds as elfsysv1.dll

Needs: WP-21, WP-25, DR-0007.
Delivers: the `newlib-cygwin` tree at `b11613e47`, vendored or fetched per
DR-0002's pattern, compiled `-mno-red-zone` throughout into a DLL named
`elfsysv1.dll` whose face is still Microsoft's — a re-badged Cygwin, not yet
re-faced. The imports route through WP-21's generated wrappers, the WP-25
counter is compiled in rather than tested beside, and `_cygtls` gains the
reserved carrier field DR-0021 left to "the forked runtime", its offset
asserted against `sizeof(_cygtls)` at build time.
Done when: a hello built against the DLL runs; the build reproduces from the
pinned ref byte-for-byte in the parts the toolchain makes reproducible; and
`runtime/tls/measure/` reruns against the forked block, closing the DR-0003
re-measurement chain against the real 3.6.10 `_cygtls` rather than 3.0.7's.
Risk: this is the first time Cygwin's source is compiled rather than called,
which is the last unreached item on spike 3's list. Expect it to find things,
and expect the `-mno-red-zone` world to surface hand-written assembly inside
winsup itself; that residue joins WP-16's ledger.

### WP-27 — the System V face at the DLL's width

Needs: WP-26, WP-22, WP-23, WP-24.
Delivers: the export surface re-faced per WP-20's inventory — System V
outward, the variadic entries from WP-24's generated veneer, the host-facing
entry points in WP-22's certified shapes now fronting the real runtime work
they were stand-ins for. Thread creation establishes the carrier for every
thread the runtime creates, which is where the veneer's `pthread_create`
inherits it (finding F8), and the per-thread split of the blocked mask and
alternate stack that DR-0030 deferred lands here with it.
Done when: WP-22's and WP-23's crossing certifications rerun unchanged
against the real DLL; `DllMain` and the PE TLS callback fire from the host's
own loader rather than from a harness; a fault beneath a System V frame still
arrives as SIGSEGV and leaves by `siglongjmp`; and a static ELF through
WP-41's branch calls a real export and returns.
Risk: the unwind seam. DR-0012's tripwire (`unwind-seam`) must hold against
gcc compiling all of winsup, not six functions, and a failure there reopens
that record on its own stated terms.

### WP-55 — the translation tables

Needs: WP-50, DR-0007.
Delivers: the divergence classes DR-0000 names, as generated tables rather
than as knowledge in someone's head — the errno value map, the signal number
map, the flag constant maps (`O_*`, `F_*`, `AT_*`, `MAP_*`, `SOCK_*` and
their relatives), and layout descriptors for the structs that cross the
boundary: `stat`, `dirent`, `termios`, the `sockaddr` family, `rlimit`,
`sigaction`, and whatever else the extraction finds differing. Each table is
extracted mechanically from el8's vendored headers on one side and the WP-26
tree's on the other, committed with a reproduce test in the WP-51 manner.
Done when: the extraction reruns byte-identically, every class DR-0000 names
has a table, and every table has a named consumer in the WP-56 shim set or a
written reason it has none.
Risk: a divergence the extraction cannot see — a field with the same name,
offset and size whose meaning differs. The differential in WP-56 is the net
under this package, not the package itself.

### WP-56 — wiring the bodies, in slices

Needs: WP-27, WP-55, WP-52 redone (below), spike 12.
Delivers: the forwards become real resolutions into `elfsysv1.dll` and the
shims become translations through WP-55's tables, sliced by subsystem —
stdio, memory, filesystem, process, sockets, time, and so on down the
headers — with the slice order taken from spike 12's demand ranking rather
than from anyone's guess. `libc-forward.tsv` stops being a promise and
becomes the generator's input.
Done when, per slice: the slice's symbols pass a differential against a real
el8 userland in the WP-T2 environment, over glibc's observable behaviour for
that slice. Done when, overall: a named small vendor package — chosen by
spike 12, built by WP-T4's harness in embryo — compiles, links, runs its own
test suite, and passes it.
Risk: this is the long pole and it always was; the point of cutting it now is
that the plan's tail stops implying otherwise. The per-slice bar keeps it
honest at every step rather than at the end.

The condensed graph in the plan gains a line:

    WP-26 ─► WP-27 ─► WP-56 ─► (WP-54, WP-62)
    WP-55 ────────────┘

## F2 — the alias rule, and WP-52 redone

`classify.py` gains an invariant: an alias's disposition is at least as
strict as its ultimate target's. A forward-alias whose target is a shim is a
shim through the same translation; a forward-alias of a stub is a stub. The
classification is regenerated, the reproduce test asserts zero violations,
and the published inventory's counts move accordingly. The known instance is
the acceptance case: after the redo, `open64` and `__open64` read as shims
over the same flag translation `open` gets, or carry a written reason they
need none (a claim the O_* table from WP-55 can settle mechanically).

This is a redo of a delivered package in the WP-50 manner, and like that one
it earns a record: a DR stating the invariant, pointing at DR-0017's
precedent for a disposition rule, and superseding nothing.

## F4 — the censuses

Three spikes and one completion. Bands first, then numbers, as always.

### Spike 12 — the demand census

Question: how many el8 packages require a symbol the classification cannot
yet stand behind? Method: one pass over the vendor binaries' undefined
versioned symbols joined against `classification.tsv`, producing a
per-package verdict and, as the useful by-product, the demand ranking WP-56's
slices are ordered by. The corpus is split by use, per the ladder: the
ranking may be read off BaseOS alone, since an ordering only has to beat a
guess and BaseOS is cheaper by the factor the DR-0002 dry run priced, while
the bands below are read against the full four-repository set and nothing
smaller, because a program-level threshold read off a partial corpus can take
the wrong branch. Bands, written before the count: under 10% of packages
touching bucket 4 reads as the tail work already planned; 10% to 40% reads as
WP-56 proceeding with a published compatibility statement naming what does
not build; over 40% is a program-level review, because at that share the
honest inventory stops being a footnote and becomes the product.

### Spike 13 — the site census

Question: over the same corpus, what share of `%fs` TLS sites are
read-modify-write, what share of those carry a `lock` prefix, what share are
the self-pointer form, and how many raw `syscall` instructions sit outside
glibc's own objects? This is the count proposal 0003 declared no longer
optional, plus the syscall half `target-definition.md` says is nobody's work
package. Its verdict lands in proposal 0003 for the TLS forms and prices, for
the first time, whether the raw-syscall bound of DR-0005 is a fence around an
empty field or around a populated one.

### Spike 14 — build throughput

Question: what does one package build cost here against the same build on
el8? One mid-size autotools package, built end to end under the pinned root,
timed, with the fork count and the peak commit recorded beside the wall
clock. The deliverable is a ratio and a decomposition, not a verdict; what it
gates is whether anything in WP-42's audit or WP-32's committed gaps needs a
performance pass before the mass rebuild, rather than during it.

### WP-16, completed

`bin/asm-ledger` runs over the el8 set. The harvester exists, the refetch is
hours, and the ledger it produces is the named residue of both the red-zone
flag and DR-0012's unwind rule. Spike 13 shares the corpus fetch, so the two
run together.

## F5 — the licensing gate

Counsel is engaged on docket items 1 and 2 of
`doc/proposals/licensing-issue.md` before WP-56's first slice ships in any
distributed artifact, and in any case within sixty days of this proposal's
acceptance. The operator owns the engagement; no agent drafts licence text,
per DR-0004. Whatever happens produces a record: an engagement noted with the
questions as put, or an explicit, dated decision to keep building at risk,
signed by the person holding that risk. The unacceptable outcome is the
current one, where the risk is carried by default and recorded nowhere but a
docket's silence.

## F6 — the document repairs, and a check that keeps them repaired

The repairs, each one edit unless noted:

- `AGENTS.md`: the "nothing has been built" sentence is replaced by a pointer
  to `Next-Steps.md`'s mechanical status; the red-zone paragraph cites
  DR-0006 and DR-0030 as settled and delivered.
- `ROADMAP.md`: the opening paragraph's claim that nothing is finished is
  qualified the same way, without turning a scope document into a status one.
- `milestones.md`: the three unnumbered spike directories gain rows — spike 9
  `vendor-image-shape`, spike 10 `ld-tls-relaxation`, spike 11
  `cygwin-from-source`, numbered in the order their transcripts first appear
  in history — and the header's count moves from eight to eleven. Spikes 12
  through 14 above land as rows when they run.
- `Next-Steps.md`: the "per spike 10" reference is repointed at the number
  the renumbering gives the spike it means.
- `IMPLEMENTATION-PLAN.md`: phase 0's "all five are done" becomes the true
  count; the new packages and the graph line above are added; the tail
  section stops at WP-62 only after passing through WP-56.
- `doc/test-environment.md`: written, since DR-0007 cites it. It separates
  the roots by their jobs as DR-0007 describes, and it is where the WP-T2
  el8 environment (F7, below) gets documented.

And the check, so the convention stops depending on memory:
`bin/check-doc-refs`, in the `check-target-definition` mould. It verifies
that every `doc/` path cited from within `doc/` and `AGENTS.md` exists, that
every directory under `spike/` has a `milestones.md` row and every cited
spike number has a directory, and it keeps proposal 0001's index-bijection
rule for the decision records. It exits non-zero on the first violation, and
the worker's merge step runs it.

## F7 — governance

Two rule changes in `AGENTS.md`'s conventions, and one sweep.

A decision record taken by an implementing agent carries `Status:
provisional` until the operator ratifies it. Ratification is cheap by design:
one sweep record can ratify many, and reopening any of them is the new-record
mechanism the index already mandates. The immediate sweep covers DR-0008
through DR-0030; its record notes, per decision, ratified or reopened, and
nothing else. `doc/decision-ladder.md`, adopted alongside this proposal,
gives future records the line the sweep reads first — the tier that
discriminated — and gives the worker a defined outcome for a choice the
tiers cannot make: park the entry with its survivors named, rather than
guess.

A certification taken against a substitute for the thing it certifies — WSL's
glibc 2.43 standing in for el8's 2.28 is the standing example — is
permitted and creates a row in a substitutions ledger,
`doc/substitutions.md`: what was substituted for what, where, and what burns
it down. WP-T2 gains the burn-down: a pinned el8-shaped environment, a Rocky
or Alma 8.10 userland with glibc 2.28 documented in `doc/test-environment.md`,
against which the WP-33, WP-35 and WP-40 differentials rerun. Divergences
found are recorded, not assumed away; the ledger row closes when the rerun
matches or when its divergence is written down as justified, which is the
same bar WP-T2 already states for every comparison it owns.

## F8 — the single-check items

The `#!` head: one `git show` against el8's kernel source at a named ref
settles `BINPRM_BUF_SIZE` for the 4.18 line RHEL actually ships, backports
included. If it reads 128, DR-0027's constant moves to 128 and the fuzz
corpus gains the 129-byte case; the record itself said the number was cheap
to change and the shape was not.

The PIE default: `target-definition.md` gains it as a sixth value, WP-13
gets the follow-up that makes the compiler's default match el8's, and
`bin/check-target-definition` covers the new literal. The choice itself is
el8's to dictate — the vendor sample was uniformly `ET_DYN` — so this is a
recording, not a decision.

Thread creation and per-thread signal state are scoped into WP-27 above,
which is where DR-0021's and DR-0030's deferrals both said they belonged once
a forked runtime existed.

Build throughput is spike 14 above.

## Sequencing

Four things start now and depend on nothing: the document repairs with
`bin/check-doc-refs`, the WP-52 alias redo, the counsel engagement, and
spike 12. Spikes 13 and 14 and the WP-16 ledger run follow on the shared
corpus fetch. WP-26 heads the runtime chain and WP-55 runs beside it; WP-27
follows WP-26; WP-56 waits on WP-27, WP-55, the redone classification and
spike 12's ranking, and its first slice waits additionally on the WP-T2
environment. Nothing already queued moves: WP-54, WP-15 and the phase 6
packages keep their places, with WP-62's confirmation now downstream of
WP-56 where it always factually was.

The queue gains, in order: the doc repairs as one entry, WP-52-redo,
spike 12, WP-26, WP-55, spike 13, WP-27, WP-56 sliced as its own sub-queue,
spike 14 anywhere after WP-41's branch can run a build.

## Cross-cutting concerns

Nothing delivered is discarded. The classifier redo changes a generated
artifact and its generator; every other correction adds. The append-only
rule holds everywhere it applies: DR-0007's missing citation is repaired by
creating the cited file, DR-0027's constant moves only on a measurement and
through its own stated mechanism, and provisional status attaches to future
records without relabelling past ones — the sweep record is what settles the
past ones. The worker keeps running throughout; the only change it feels is
`bin/check-doc-refs` in its merge step and the new entries in its queue.

Cost, honestly. WP-26 and WP-27 are weeks of work that were always owed and
never priced; WP-56 is the long pole restored to visibility rather than new
scope; the spikes are days each on a corpus fetch that WP-16 needs anyway.
The one genuinely new cost is the differential environment, and it replaces
a substitution three delivery notes already apologized for.

## Verification criteria

1. Every finding F1 through F8 in `findings.md` is named by exactly one
   correction section here, and grep confirms the mapping.
2. `bin/check-doc-refs` exists, exits 0 over the repaired tree, and exits
   non-zero when any repaired file is reverted.
3. `milestones.md` has one row per directory under `spike/`, and no document
   under `doc/` cites a spike number without a row.
4. `classify.py` refuses a classification in which any alias is less strict
   than its ultimate target; the regenerated `classification.tsv` contains
   zero such rows; the `open64` row is no longer a bare forward.
5. `IMPLEMENTATION-PLAN.md` carries WP-26, WP-27, WP-55 and WP-56 in the
   plan's four-line format, and its condensed graph routes the tail through
   WP-56.
6. `doc/target-definition.md` carries the PIE default and
   `bin/check-target-definition` fails on an uncited use of it.
7. `AGENTS.md` no longer asserts that nothing has been built, no longer
   carries the red-zone repair as open, and states the provisional-status
   and substitution-ledger conventions.
8. `doc/substitutions.md` exists with a row for the glibc 2.43 differentials,
   and `doc/test-environment.md` exists and is cited by DR-0007's readers
   without a dangling path.
9. The decisions index shows a sweep record covering DR-0008 through
   DR-0030, and each covered record's status line reflects it.
10. A dated record exists for the counsel engagement, or a dated,
    operator-signed acceptance of proceeding without it.
11. `doc/decision-ladder.md` exists, `AGENTS.md`'s conventions name it, and
    the resolutions above each name the tier that discriminated.

## Open questions, run through the decision ladder

The first draft of this proposal left four questions open. On 2026-08-30 the
operator adopted `doc/decision-ladder.md`, and running the four through it
closed three outright and split the fourth into two questions that each
close. The tier that discriminated is named in each case, the convention the
ladder itself asks for.

The WP-56 slice granularity closes at tier 1. A demand-cluster slice can cut
across a subsystem, and a slice that certifies half a subsystem passes its
differential while the invariants the subsystem shares — one errno map, one
`FILE` — sit half-wired behind it; constraining the clusters to whole
subsystems to repair that is the subsystem option under another name. One
candidate remains. Slice by subsystem, with the census ordering the slices,
which tier 6 permits because ordering costs nothing secured above it; the
WP-56 section already reads this way and now does so on the record.

The spike 12 corpus was two questions wearing one sentence, and the ladder
pulled them apart. For the demand ranking, tier 7's reasonable default is
BaseOS first: an ordering only has to beat a guess. For the band verdict,
tier 1 eliminates the partial corpus, since a program-level threshold read
off a non-representative share cannot be made correct for the branch it
gates. Each half ends with one candidate, and the spike section states the
split.

The WP-26/27 shape closes at tier 5. A failure inside a merged package
cannot say whether winsup stopped compiling or the re-facing broke what
compiled; the re-badged intermediate is exactly the evidence a developer
needs in hand, and it is spike 3's one-width-first lesson applied again.
Two packages. Merging later is one delivery note and splitting later is
archaeology, so the proportionality rule points the same way.

The substitutions ledger closes at tier 7. Nothing above it discriminates
between a document and heartbeat rows, and the reasonable default for three
rows is a document.

Two standing questions elsewhere in `doc/` were run through the ladder while
it was out. WP-61's core-dump decision closes at tier 5: a minidump of a
stub survives correctness only when wrapped in conversion tooling that would
have to know everything an ELF core writer knows, and the ELF core is what
leaves the link map, the symbols, and the thread state where the triple's
gdb can read them. That resolution feeds WP-61's own record when the package
runs; nothing here reorders phase 6. Proposal 0003's choice between
emulating `EFLAGS` and demanding an exhaustive rewriter reaches tier 8 and
stops there, which is the ladder agreeing with the sequencing above: spike
13's census is what discriminates, and until it runs, stopping is the
decision.

What remains open is what the ladder cannot reach. The counsel questions of
DR-0004 are not candidate-option questions, and every number this proposal
sends to a spike stays a number until the spike produces it.
