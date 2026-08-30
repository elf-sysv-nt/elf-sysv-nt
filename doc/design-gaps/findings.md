# Design review — findings

Reviewed 2026-08-30 against `3069214` on `main`. Everything under `doc/` was
read in full: the three surveys, the target definition, the milestones, the
roadmap, the implementation plan, the thirty-one decision records, the five
proposals, and the two generated inventories. Where a finding turns on the
tree rather than on the documents, the tree was checked: the classification
table, the semantic-review seed, the spike directories, and the build queue
were each read directly. `doc/design-gaps/proposal.md` carries the
corrections; this file carries only what was found.

The verdict comes first, because the findings below would otherwise
misrepresent it. The design discipline is real and it holds. Spikes run before
commitments, reopen bands are written before their numbers exist, records are
append-only, Not verified sections stay current, and done-conditions pass or
fail rather than persuade. The loader, the TLS carrier, the ABI crossing and
the process integration all stand on measurement. Every problem below clusters
where the documents go quiet, which is itself a kind of evidence that the
discipline works: the gaps sit in what was deferred, not in what was decided.

Findings are ordered by weight. F1 through F5 are load-bearing, F6 and F7 are
institutional, and F8 collects smaller items that each want one check.

## F1 — the semantic face has no work package

Every body in the delivered `libc.so.6` is a `ret`; DR-0026 says so in as many
words, and `veneer/libc/libc-forward.tsv` records the intended target of each
entry without wiring any. What remains is the wiring of 353 renamed forwards,
1614 same-name forwards, and 192 shims — and behind the shims, the translation
DR-0000 itself names as the real work: errno values, signal numbers, struct
layouts, flag constants, all presented as Linux's exactly and translated up
from Cygwin. No work package owns any of it. The plan's tail reads WP-54, then
WP-62 and WP-63, plus gdb and core dumps, as though the trunk were finished
once its export surface linked. A reader of `Next-Steps.md` who trusts the
`✓` marks would conclude the program is weeks from a package build. It is not,
and the distance is the largest unpriced quantity in the tree.

## F2 — aliases do not inherit their base's shim disposition

A concrete defect in the delivered classification, found by reading four rows.
`open` is bucket 3, a shim, with the note that O_* flag values differ between
the two worlds. Its neighbours `__open`, `__open64` and `open64` are bucket 1,
forward-aliases resolving straight onto the runtime's `open`, with no flag
translation anywhere on the path. An el8 object compiled with
`_FILE_OFFSET_BITS=64`, which is most of them, calls `open64` rather than
`open`, so the common case bypasses the shim the uncommon case gets. The
pattern is structural rather than local: the semantic-review seed was curated
against base names, the alias rows were classified mechanically by name match,
and nothing checks that an alias is at least as strict as its target. The 353
forward-alias rows all deserve the same suspicion, `__xstat`'s relatives
among them.

## F3 — rebuilding winsup as `elfsysv1.dll` is not in the plan

DR-0000 states the runtime body is Cygwin's `winsup/cygwin` at the DR-0007
ref, compiled unchanged beneath a re-faced surface. No work package builds it.
WP-22 certified stand-in entry points at one function's width; DR-0021 defers
the carrier field to "the forked `elfsysv1.dll`"; every delivered package runs
against the stock `cygwin1.dll`. The list spike 3 could not reach — unwind
data through `sysv_abi` frames, `DllMain` and PE TLS callbacks into a
System V-faced DLL, Cygwin's source rebuilt rather than called — is exactly
what the plan's own Not verified section says phase 2 meets first, and none of
the delivered phase 2 packages met it. The single largest chunk of the
critical path is invisible in the graph that claims to be the critical path.

## F4 — no demand-side census exists

`what-the-veneer-lacks.md` counts 688 public-absent symbols, `epoll`,
`inotify`, `eventfd`, `signalfd`, `timerfd_create`, `getauxval` and `syscall`
among them, and `syscall` is worth a sentence of its own: it is a public
export a great many real packages call directly, it is a bucket-4 stub, and
the bounded-linux claim of DR-0005 does not cover it, since a rebuilt package
calling the libc function has used no raw `syscall` instruction. Nobody has
counted how many of the 2893 el8 packages require a bucket-4 symbol. That
count is one pass over the vendor binaries' undefined symbols against the
classification, it is cheap in exactly the way spike 5 was cheap, and it could
reshape the program the way spike 5 validated the triple. Two further
censuses are named in the tree and unrun: WP-16's assembly ledger, which its
own delivery note admits "has not yet been pointed at the thing it exists
for", and the `%fs`-site census proposal 0003 calls "no longer optional".

## F5 — the licensing risk grows while delivery accelerates

DR-0004 reserves two questions for counsel. Whether a modified Cygwin library
may carry the linking exception forward, and whether an LGPLv3 runtime beneath
a GPLv2-only userland is a lawful combination in the shape this project
actually ships. The docket in `doc/proposals/licensing-issue.md` is thorough,
and nothing anywhere records counsel being engaged. "Nothing in phase 1
depends on the answer" was true when DR-0004 was written; with WP-53 delivered
and an autonomous worker draining the queue, the sunk cost against an
unanswered program-killing question compounds daily. DR-0004's own words for
the bad outcome are that the platform could run the el8 userland technically
and not lawfully, which is a different program.

## F6 — the governing documents have rotted against their own convention

The convention in `AGENTS.md` is that a change updates the governing document
in the same change, never after. Six violations stand. `AGENTS.md` line 11
still says nothing has been built. `AGENTS.md` still carries the choice
between `-mno-red-zone` and a delivery-path repair as open, though DR-0006
settled the direction and WP-43 delivered the repair. `ROADMAP.md` still
opens by saying nothing is finished. `milestones.md` says all eight spikes
have run while the tree holds eleven spike directories, three of them
(`vendor-image-shape`, `ld-tls-relaxation`, `cygwin-from-source`) with no row
anywhere, and `Next-Steps.md` cites a "spike 10" no document defines.
`IMPLEMENTATION-PLAN.md`'s phase 0 still says all five are done. DR-0007
names `doc/test-environment.md` as a place it is written down. That file does
not exist.

## F7 — governance has drifted behind the worker

Of the thirty-one decision records, roughly twenty were taken by implementing
agents across two days, each marked "the operator may ratify or reopen", and
no ratification has happened. The same agents wrote the bars their work was
then certified against. Separately, the differential certifications in WP-33,
WP-35 and WP-40 compare against Ubuntu's glibc 2.43 through WSL rather than
el8's 2.28; each delivery note acknowledges the substitution, and the
acknowledgment recurring three times is the point — the comparison
environment the program actually needs is pinned nowhere, while the RHEL root
exists precisely to be the verification floor.

## F8 — smaller items, one check each

The `#!` head. DR-0027 adopts 256 bytes as "the kernel's number" and refuses
longer lines. Current Linux reads 256; the 4.18 line el8 ships used a
128-byte `BINPRM_BUF_SIZE`, with the raise arriving in 5.1. If el8's kernel
truly reads 128, this platform accepts scripts el8 refuses, which is the
pleasant-direction compatibility bug DR-0027 itself warns against. One
`git show` against the el8 kernel source at a named ref settles it, RHEL
backports included.

The PIE default. `target-definition.md` fixes five values; a sixth ends up in
every artifact just as surely, namely whether the compiler links PIE by
default. el8's gcc does, and every object in `spike/vendor-image-shape/` was
`ET_DYN`. Nothing records what WP-13's compiler defaults to, and the answer
decides how often DR-0028's low-window machinery matters at all.

Thread creation. Carrier C3 requires a runtime-owned stack (DR-0021), and
nothing names the veneer's `pthread_create` as the site where a vendor-created
thread gets its pointer established. The per-thread signal state carries the
same deferral in DR-0030's Not verified list.

Build throughput. The program's acceptance criterion is a mass rebuild, on a
platform whose `fork` is slow by inheritance, made slower by the WP-42 audit,
with cross-process text sharing given up by WP-32. Nobody has priced a
package build end to end. The number is as decision-worthy as anything a
spike has produced.

## What this review did not reach

The spike transcripts were not re-executed and the certifications were not
rerun; every "delivered" claim was taken at its word, with the review reading
the claims against each other rather than against the machines. The
`semantic-review.tsv` seed was read for the rows findings F2 turns on, not
audited in full. No el8 package was built, so F1's distance is asserted from
the shape of the work rather than measured — measuring it is what the
proposal's census exists to do. And the licensing findings state that no
engagement is recorded, which is weaker than stating none occurred.
