# Deferred work nobody owns

Every item here was deferred on purpose, by a document that said so, and then
lost the package that was going to do it. Most of them followed the same
route: a decision record pushed the work to a work package, the package was
delivered, and the deferral was never re-homed. `DR-0024` sends late
initial-exec TLS offsets to "WP-38's `dlopen`", WP-38 is delivered, and
`loader/dl/` has no initial-exec path. `DR-0029` leaves the loader-lock
bracket as "work this package identifies and does not do"; WP-38 and WP-42 are
both delivered. `DR-0041` says whoever repairs the `ucontext` triple should
reopen it, and whoever is nobody.

The convention that produced this is a good one — a record names what it does
not decide rather than pretending to have decided it — and the failure is at
the other end, where nothing reads those sections back. This file is that
read-back, taken 2026-09-02 against `ab2918b`. It is a register, not a plan:
an entry here is findable, not scheduled. When one gets a work package, cut it
from this file and let the plan carry it, so the two never disagree about who
owns what.

Three entries were live functional breaks when this was compiled and became
WP-46, WP-47 and WP-48 the same day; they are marked rather than cut, because
the shape of how they went unowned is the point.

## Live breaks

These are not gaps in polish. Each has a named failure mode and a document
that describes it happening.

**The Cygwin `child_info` handshake collides.** Spawned from a Cygwin parent,
bash passes its cygheap, the badge reads it, and the process dies.
`runtime/winsup/README.md:26-27` calls handshake separation "re-face work,
deferred" — and WP-26 and WP-27, the re-face packages, are both delivered.
Now WP-46.

**The `%fs` TLS rewriting subsystem was never cut into a package.**
`doc/IMPLEMENTATION-PLAN.md:59-70` says so in as many words: "cutting it into
a package is not done here." Nothing downstream picks it up, and yet WP-33's
and WP-54's exit criteria both run a vendor binary, which is exactly the case
that needs it. The read-modify-write gap and its `SIGSEGV` failure mode are at
`doc/milestones.md:390-398`. Now WP-47, with the census below folded in.

**Spike 13's site census lost its row.** The plan cuts it at
`doc/IMPLEMENTATION-PLAN.md:95-99` — the read-modify-write, `lock`-prefixed and
self-pointer shares, plus the raw-`syscall` count that prices DR-0005's
bound — but `doc/milestones.md:43` row 13 is now `spike/reent-bringup/`, and
the census owns no row and no package. `doc/milestones.md:396` still reads "a
census nobody has run." Folded into WP-47, since it is what sizes that hole.

**`getcontext`, `setcontext` and `swapcontext` are knowingly broken across the
face.** `doc/decisions/0041-context-transparent-faces.md:53-59`: "They are
deferred, not settled: a written face that captures at the seam is the likely
repair, and whoever takes it should reopen this record." No package names
`ucontext` outside WP-43's signal-frame layout. Now WP-48.

## Correctness gaps still unowned

**The `SA_RESTART` down-call wrapper is not written.**
`doc/decisions/0030-the-shape-of-a-signal-delivery.md:151-154`, restated at
`doc/IMPLEMENTATION-PLAN.md:1167-1170` under "What is not here". WP-21 wrote
the wrappers and WP-43 the signals; both delivered. DR-0009 is a convention,
not a package.

**A signal that lands inside `cygwin1.dll` is not deferred.** Cygwin's
`interrupt_now` defers; this package redirects wherever the target happens to
be (`0030:139-143`). WP-43 delivered.

**The loader-lock bracket does not move inside `dl_open`/`dl_close`.**
`doc/decisions/0029-what-crosses-the-fork-and-how-it-is-checked.md:98-104`,
echoed at `doc/IMPLEMENTATION-PLAN.md:1111-1113`.

**Static-offset assignment for a late initial-exec module.**
`doc/decisions/0024-static-tls-surplus-and-dtv-shape.md:61-63` and
`doc/IMPLEMENTATION-PLAN.md:856` both point at WP-38, which is delivered
without such a path.

**`__libc_start_main` is not adopted in the startup files.**
`doc/decisions/0048-main-returns-through-exit.md:29-31` — "stays open; when it
lands, the call to `exit` moves into it." Nothing mentions it.

**Exact `long double` across the core `va_list` seam.**
`doc/decisions/0015-variadic-rebuild-through-a-core-valist.md:61-64`. The walk
narrows to `double`; whether the runtime needs more, and at what cost, is
open. WP-24 delivered.

**`fnmatch`'s flag bits are swapped between el8 and Cygwin.**
`doc/decisions/0056-the-stat-family-does-not-forward.md:79-87` leaves the shim
to `diff-slice.sh`, "where a differential will show it" — a tool, not an
owner.

**Fortified entry points under `__USE_FORTIFY_LEVEL` remain deferred.**
`veneer/README.md:108-110`. WP-50 delivered; WP-56's slice text never names
fortification.

**`XCRYPT_2.0` may have no body, and the companion set closed by omission.**
`doc/decisions/0013-version-map-companion-sources.md:64-74` makes
`ld-linux`, `libnss_*`, `libmvec` and `libanl` "WP-54's scope call"; WP-54 is
delivered and DR-0032 fixed the set at eight without answering the question.
`crypt` and `crypt_r` carry real el8 demand.

**Exec's inherited obligations have never been tested across the ELF branch** —
descriptor inheritance and close-on-exec, cwd, signal disposition,
environment — and whether Cygwin's spawn path can call the classifier without
disturbing `#!` is untested.
`doc/decisions/0027-the-exec-branch-and-the-interpreter-limit.md:92-99`.

## Measurements and censuses nobody owns

**The `[0, 4 GB)` walk at process init is not built**, because it needs a
census of what is legitimately mapped low first.
`doc/decisions/0072-the-low-window-belongs-to-the-guest.md:108-114`.

**Whether a larger `MEM_RESERVE` is honored at `_dll_crt0`**, which is what
would let the reserved window widen from 1 GB toward the contract line.
`0072:118-120`.

**The `ET_EXEC` share of el8.** `0072:122-126`;
`spike/vendor-hardened-build/` measures one package.

**`0x3FC00000` is a judgment, not a measurement.**
`doc/decisions/0028-the-low-window-is-reserved-by-the-parent.md:99-102`.

**Nothing injects an allocation between the window's release and placement**,
and nothing proves Cygwin has no timer or worker thread running by then
(`0028:87-92`, "reasoned rather than measured").

**The committed-gap cost of one-region-per-object mapping.**
`doc/decisions/0008-mmap-granule-protection.md:85-89`.

**The `.gnu.version` map's real size is an estimate.**
`doc/ROADMAP.md:543-544`. WP-51 delivered.

**Nobody has grepped a distribution for an `ELFSYSVNT` collision.**
`doc/target-definition.md:237-239` — "listed rather than done."

**The four-hop interpreter limit was matched to Linux from memory.**
`doc/decisions/0027-...md:87-90`.

**The fork rebase result is one machine, one day, no ASLR variation.**
`doc/decisions/0029-...md:111-115`.

**`iretq` under a hardened Windows is untested.**
`doc/decisions/0030-...md:134-137`. DR-0062 opts out at the compiler and says
it does not cover the host setting (`0062:46-49`).

**Cygwin's `fork` replaying every `mmap` mapping is asserted, not measured**,
and WP-42 rests on it. `doc/ROADMAP.md:539-541`.

**The twenty packages whose `config.sub` refuses the triple have not been
reconfigured after a refresh.** `toolchain/config/README.md:89-92` names
`perl-Tk` and `autoconf213` as the ones to try.

**The core-dump fatal-path wiring is unwritten**, certified only against
synthetic images. `doc/decisions/0033-an-elf-core-from-the-runtime.md:74-77`;
WP-61 is delivered and the plan still reads "Open rather than planned."

**Compiler-side enforcement of no `%fs`-relative TLS is left to the operator**,
and whether the image scan belongs to the acceptance harness or the package
build is undecided.
`doc/decisions/0063-images-carry-no-fs-relative-tls.md:61-63`.

**The 128-byte gap in the delivery path has never been priced.**
`doc/IMPLEMENTATION-PLAN.md:44-45`. A reserved call, deliberately not a task,
but with no trigger that would raise it.

## Stale prose, not open work

Two places describe a question as open that a later record answered. Correcting
them is a five-minute job for whoever is next in the file.

`doc/what-a-stub-means.md:113-117` and
`doc/decisions/0052-a-stub-may-be-filled-with-a-synthesized-body.md:53-56` both
say the acceptance verdict for a filled stub "is left to a follow-up". DR-0057
settled it on 2026-09-01: `ready` is forward, wired or filled, and bzip2 reads
34/5/1 against that rule.

`doc/ROADMAP.md:371` says hand-written assembly is where the red zone stays
open. DR-0050 records WP-16's ledger as having closed that bound.

The psABI citation in `doc/elf-technical-breakdown.md` pointed at
`uclibc.org/docs/psABI-x86_64.pdf`, a 2012 snapshot, while the document itself
is maintained live at `gitlab.com/x86-psABIs/x86-64-ABI`. Corrected the same
day, along with the observation that none of this material is an RFC and that
symbol versioning is in no ABI document at all — it is a GNU extension, and
Drepper's two papers are its specification.

## Not verified

That this list is complete. It was compiled by searching for deferral language
— "left open", "deferred", "nobody has", "stays open", and the rest — and by
reading every "What it does not decide" section in `doc/decisions/`. A
deferral phrased in words that search did not cover is still out there, and the
only honest way to find it is to read the tree rather than grep it.

That each entry's owning package is really absent rather than merely unnamed.
The check was a search of `doc/IMPLEMENTATION-PLAN.md` for each item's
vocabulary. A package whose text covers an item in different words would read
here as unowned when it is not.
