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
`veneer:doc/IMPLEMENTATION-PLAN.md:59-70` says so in as many words: "cutting it into
a package is not done here." Nothing downstream picks it up, and yet WP-33's
and WP-54's exit criteria both run a vendor binary, which is exactly the case
that needs it. The read-modify-write gap and its `SIGSEGV` failure mode are at
`doc/milestones.md:390-398`. Now WP-47, with the census below folded in.

**Spike 13's site census lost its row.** The plan cuts it at
`veneer:doc/IMPLEMENTATION-PLAN.md:95-99` — the read-modify-write, `lock`-prefixed and
self-pointer shares, plus the raw-`syscall` count that prices DR-0005's
bound — but `doc/milestones.md:43` row 13 is now `veneer:spike/reent-bringup/`, and
the census owns no row and no package. `doc/milestones.md:396` still reads "a
census nobody has run." Folded into WP-47, since it is what sizes that hole.

**`getcontext`, `setcontext` and `swapcontext` are knowingly broken across the
face.** `veneer:doc/design/decisions/0041-context-transparent-faces.md:53-59`: "They
are deferred, not settled: a written face that captures at the seam is the
likely repair, and whoever takes it should reopen this record." No package
names `ucontext` outside WP-43's signal-frame layout. Now WP-48.

## Correctness gaps still unowned

**May the veneer call NT directly, or must every host call reach the OS through
`elfsysv1.dll`?** Nothing forbids it. The one auditable no-Win32 criterion,
`veneer:doc/IMPLEMENTATION-PLAN.md:369`, is WP-21's and is scoped to `runtime/`; what
is recorded is narrower, `veneer/wiring/wire.h` and DR-0049 keeping the wiring
layer free of Windows headers so it certifies under a host compiler with a fake
resolver, which is testability rather than layering. DR-0065 already has a
veneer body walking the faced DLL's PE export directory, so host-format
knowledge inside the veneer is established practice.

The question is therefore one host chokepoint or two. WP-21 built one — 370
imports, a single generated wrappers unit, and `runtime/imports/audit-imports.sh`
failing any object but that unit which names an import. A veneer-side Win32
call would be the first object outside it to name an import and no audit would
notice. Against a second: 1001 of Cygwin's 1767 exports are `SIGFE`, wrapped in
a signal frame that makes them interruptible, and a call entering through the
runtime is inside that envelope where a raw one is not; DR-0029's fork replay
likewise does not know about state the runtime did not create. For it: some
checks have no counterpart to forward to — `mincore` classifies bucket 4, and
`mprotect` and `msync` do not report protection — so routing through the
runtime means adding an export and changing a shipped surface.

Every argument against is about state and blocking, and a pure non-blocking
query of the process's own address space has neither. So the likely answer is a
predicate rather than a side: direct calls for pure queries, everything else
through the runtime, with WP-21's discipline mirrored — a declared import list,
one injected seam so the fake-resolver certification survives, and a link-map
audit. Three measurements are owed first: whether a raw `kernel32` call from
System V veneer code in a faced process works at all (it needs an `ms_abi`
thunk and a PE export walk, both present in the tree, neither done from the
veneer); whether Cygwin expects anything of *any* NT entry beside it; and how
many shim families would ever want this, since if the answer is one, a single
runtime export is cheaper than a new discipline.

On no package's path. Raised while settling `__fprintf_chk`, which turned out
not to need it: no `_chk` body Cygwin ships performs the `%n` check, so a
veneer matching its siblings is consistent rather than weak.

**Closed 2026-09-03 by DR-0085.** **A shim is credited `wired` without a body.** `acceptance/accept.sh:281-296`
takes the union of `wire-<slice>.shims.tsv` over slices carrying a
`live-<slice>.sh` and calls every symbol in it `wired`, on the stated ground
that "a written translation stands behind each". Nothing tests that. Measured
2026-09-02: 122 shim rows are credited across the crossed slices, and the
sample checked has no bodies — `__errno_location` and `open64` have no
implementation anywhere under `veneer/` or `runtime/`, and the only
hand-written shim family in the tree is `wire-jmpbuf-face.inc`. A slice
becomes certified by the existence of its `live-<slice>.sh`, and
`live-string.sh` proves only that its one shim is correctly *absent* from the
bind. The root-cause repair is to credit `wired` against a curated bodies
manifest, the way `filled` already works through `*-filled.tsv`; it re-verdicts
122 symbols and belongs to whoever owns the acceptance witness. Found while
attributing the fortify family to its slices, which would have extended the
credit by 42 more rows.

**`DT_FINI` and `DT_FINI_ARRAY` are not run for anything the exec path loaded.**
`loader/dl/dl_init.c` runs fini correctly, the array reversed and then
`DT_FINI`; `loader/exec/dyn_init.c` has the init half and no fini half at all,
and nothing plays the part `_dl_fini` plays on Linux. C++ static destructors and
anything a loaded object registers for its own teardown never run.
`veneer:doc/IMPLEMENTATION-PLAN.md` § WP-45 names it and orders it behind WP-56, which
is why WP-45 landed without it on 2026-09-03: DR-0048 puts the atexit chain in
glibc's own `exit`, so the loader's fini has to register through the veneer's
`__cxa_atexit`, and that body is not live. This entry is the register the plan's
ordering was relying on and did not have; it is cut when WP-56 lands and the
work becomes schedulable, or when a package takes it.

**The `SA_RESTART` down-call wrapper is not written.**
`doc/design/decisions/0030-the-shape-of-a-signal-delivery.md:151-154`, restated
at `veneer:doc/IMPLEMENTATION-PLAN.md:1167-1170` under "What is not here". WP-21 wrote
the wrappers and WP-43 the signals; both delivered. DR-0009 is a convention,
not a package.

**A signal that lands inside `cygwin1.dll` is not deferred.** Cygwin's
`interrupt_now` defers; this package redirects wherever the target happens to
be (`0030:139-143`). WP-43 delivered.

**The loader-lock bracket does not move inside `dl_open`/`dl_close`.**
`doc/design/decisions/0029-what-crosses-the-fork-and-how-it-is-checked.md:98-104`,
echoed at `veneer:doc/IMPLEMENTATION-PLAN.md:1111-1113`.

**Static-offset assignment for a late initial-exec module.**
`doc/design/decisions/0024-static-tls-surplus-and-dtv-shape.md:61-63` and
`veneer:doc/IMPLEMENTATION-PLAN.md:856` both point at WP-38, which is delivered
without such a path.

**`__libc_start_main` is not adopted in the startup files.**
`veneer:doc/design/decisions/0048-main-returns-through-exit.md:29-31` — "stays open;
when it lands, the call to `exit` moves into it." Nothing mentions it.

**Exact `long double` across the core `va_list` seam.**
`doc/design/decisions/0015-variadic-rebuild-through-a-core-valist.md:61-64`.
The walk narrows to `double`; whether the runtime needs more, and at what cost,
is open. WP-24 delivered.

**`fnmatch`'s flag bits are swapped between el8 and Cygwin.**
`veneer:doc/design/decisions/0056-the-stat-family-does-not-forward.md:79-87` leaves
the shim to `diff-slice.sh`, "where a differential will show it" — a tool, not
an owner.

**Fortified entry points under `__USE_FORTIFY_LEVEL` remain deferred.**
`veneer/README.md:108-110`. WP-50 delivered; WP-56's slice text never names
fortification.

**`XCRYPT_2.0` may have no body, and the companion set closed by omission.**
`veneer:doc/design/decisions/0013-version-map-companion-sources.md:64-74` makes
`ld-linux`, `libnss_*`, `libmvec` and `libanl` "WP-54's scope call"; WP-54 is
delivered and DR-0032 fixed the set at eight without answering the question.
`crypt` and `crypt_r` carry real el8 demand.

**Exec's inherited obligations have never been tested across the ELF branch** —
descriptor inheritance and close-on-exec, cwd, signal disposition,
environment — and whether Cygwin's spawn path can call the classifier without
disturbing `#!` is untested.
`doc/design/decisions/0027-the-exec-branch-and-the-interpreter-limit.md:92-99`.

## Measurements and censuses nobody owns

**The `[0, 4 GB)` walk at process init is not built**, because it needs a
census of what is legitimately mapped low first.
`veneer:doc/design/decisions/0072-the-low-window-belongs-to-the-guest.md:108-114`.

**Whether a larger `MEM_RESERVE` is honored at `_dll_crt0`**, which is what
would let the reserved window widen from 1 GB toward the contract line.
`0072:118-120`.

**The `ET_EXEC` share of el8.** `0072:122-126`;
`spike/vendor-hardened-build/` measures one package.

**`0x3FC00000` is a judgment, not a measurement.**
`doc/design/decisions/0028-the-low-window-is-reserved-by-the-parent.md:99-102`.

**Nothing injects an allocation between the window's release and placement**,
and nothing proves Cygwin has no timer or worker thread running by then
(`0028:87-92`, "reasoned rather than measured").

**The committed-gap cost of one-region-per-object mapping.**
`doc/design/decisions/0008-mmap-granule-protection.md:85-89`.

**The `.gnu.version` map's real size is an estimate.**
`doc/ROADMAP.md:543-544`. WP-51 delivered.

**Nobody has grepped a distribution for an `ELFSYSVNT` collision.**
`doc/design/target-definition.md:237-239` — "listed rather than done."

**The four-hop interpreter limit was matched to Linux from memory.**
`veneer:doc/design/decisions/0027-...md:87-90`.

**The fork rebase result is one machine, one day, no ASLR variation.**
`veneer:doc/design/decisions/0029-...md:111-115`.

**`iretq` under a hardened Windows is untested.**
`veneer:doc/design/decisions/0030-...md:134-137`. DR-0062 opts out at the compiler and
says it does not cover the host setting (`0062:46-49`).

**Cygwin's `fork` replaying every `mmap` mapping is asserted, not measured**,
and WP-42 rests on it. `doc/ROADMAP.md:539-541`.

**The twenty packages whose `config.sub` refuses the triple have not been
reconfigured after a refresh.** `toolchain/config/README.md:89-92` names
`perl-Tk` and `autoconf213` as the ones to try.

**The core-dump fatal-path wiring is unwritten**, certified only against
synthetic images.
`doc/design/decisions/0033-an-elf-core-from-the-runtime.md:74-77`; WP-61 is
delivered and the plan still reads "Open rather than planned."

**Compiler-side enforcement of no `%fs`-relative TLS is left to the operator**,
and whether the image scan belongs to the acceptance harness or the package
build is undecided.
`doc/design/decisions/0063-images-carry-no-fs-relative-tls.md:61-63`.

**The 128-byte gap in the delivery path has never been priced.**
`veneer:doc/IMPLEMENTATION-PLAN.md:44-45`. A reserved call, deliberately not a task,
but with no trigger that would raise it.

## Stale prose, not open work

Two places describe a question as open that a later record answered. Correcting
them is a five-minute job for whoever is next in the file.

`veneer:doc/design/what-a-stub-means.md:113-117` and
`veneer:doc/design/decisions/0052-a-stub-may-be-filled-with-a-synthesized-body.md:53-56`
both say the acceptance verdict for a filled stub "is left to a follow-up".
DR-0057 settled it on 2026-09-01: `ready` is forward, wired or filled, and
bzip2 reads 34/5/1 against that rule.

`doc/ROADMAP.md:371` says hand-written assembly is where the red zone stays
open. DR-0050 records WP-16's ledger as having closed that bound.

The psABI citation in `doc/history/elf-technical-breakdown.md` pointed at
`uclibc.org/docs/psABI-x86_64.pdf`, a 2012 snapshot, while the document itself
is maintained live at `gitlab.com/x86-psABIs/x86-64-ABI`. Corrected the same
day, along with the observation that none of this material is an RFC and that
symbol versioning is in no ABI document at all — it is a GNU extension, and
Drepper's two papers are its specification.

## Found writing the errno and signal shims, 2026-09-03

Three items were registered here, all in `veneer/wiring/gen-xlat.py` and the
tables it reads, and all three are now repaired rather than deferred. They are
cut per this file's own convention and recorded here in one line each so the
trail from the finding to the fix survives the cut.

The generator emitted `short` cells without checking that a value fit, so
`SIGSTKSZ`'s 32768 stored as −32768; it now refuses an unfitting row at
generation time instead of truncating. `extract-tables.py` had scraped
`SIGSTKSZ` into `signal-map.tsv` as though a stack size were a signal, which
is what made `xl_signal_up` 32769 `short`s wide to carry thirty-one signals;
the row is gone and `dropped.tsv` records why. And an el8 constant with no
runtime equivalent no longer passes through onto a numeral that is already
taken — it declines. DR-0092 settles that last one and explains why declining
rather than dropping is the right shape for a value that is the whole answer.
`veneer/wiring/t/xlat-pairs.sh` pins all three, the decline set swept closed
in both directions rather than sampled.

Two shims are blocked rather than deferred and are named here so they are
findable: `__errno_location` is parked at ladder tier 8 by DR-0088, and
`signal` waits on three pieces of `runtime/signal/` wiring named in DR-0089.
Neither is in `veneer/wiring/bodies.tsv` and neither should be until it has a
body.

## Found regenerating the wiring, 2026-09-03

**Eighteen live-crossing suites reference wiring that no longer exists.**
DR-0090 removed the committed `wire-<slice>.gen.*` files for the seventeen
slices DR-0083 emptied, and `wire-jmpbuf-faces.gen.S` with them.
`veneer/wiring/t/live-<slice>.sh` builds its slice's generated assembly, so
seventeen of those scripts and `t/live-jmpbuf.sh` now name a file that is not
there. None of them can measure anything either way — a slice with no rows has
no crossing to make — so the work is to withdraw or rewrite them rather than
to repair the reference. They are `report`-tier, unregistered individually in
`test/suites.tsv`, and reached only through `veneer/wiring/t/run-tests.sh`,
which is the suite that reds because of this. Nobody owns it.

Three decision records cite files that removal took away — DR-0051 names
`wire-runtime.gen.s`, DR-0053 `wire-wchar.gen.s` and `t/live-wchar.sh`,
DR-0054 `wire-terminal.gen.s` and `t/live-terminal.sh`. Records are
append-only and were not edited; the citations are history rather than live
paths, and `bin/check-doc-refs` does not police them because it checks only
`doc/`, `bin/`, `spike/` and `ci/` paths. That exemption is doing more work
than it was written to do, and it is a question for whoever next edits that
checker.

## Found repairing the specs file, 2026-09-07

**WP-15's exit criterion is checked at link time and has never run.**
`toolchain/gcc/t/accept2.sh` says so itself -- "the catch itself runs when the
loader can run" -- so every claim it makes is about a section or a segment in a
linked object. DR-0108 restored `PT_GNU_EH_FRAME` and the shared libgcc, which
is what those claims read, and what they assert is that the unwind tables will
be findable. A throw crossing a DSO has never executed on this platform, and
cannot until the core runs a dynamic program. Nothing owns the run-time half,
and the green suite is the thing most likely to be mistaken for it.

**The report tier has no moment at which it runs.** `bin/nightly-full` exists
for exactly this, runs `bin/run-suites --tier report`, and is not scheduled on
any machine. `accept2.sh` was red from the 2026-09-06 gcc remake until
2026-09-07 and nobody learned anything from it, which is what an unscheduled
backstop costs. The scheduling is machine state rather than repository state,
so it is not a file anybody can land; it is an act somebody has to take on the
build host, and this entry exists so the omission is findable when it recurs.

**`bin/check-target-definition` is red on the trunk and has been.** It reports
seventeen files carrying a target value without naming
`doc/design/target-definition.md`, at `ce566cb` and at every commit this
session added; `toolchain/gdb/`, `toolchain/rpm/surface/`,
`toolchain/sysroot/` and `bin/elf-build-worker` are among them. The checker is
doing its job -- the exemption it grants is attribution, and these files have
none -- so the work is to attribute each site or to decide the literal is
incidental there, seventeen small judgments rather than one. It is not in
`ci/suites.txt`, which is why nothing has said so. Nobody owns it.

## Not verified

That this list is complete. It was compiled by searching for deferral language
— "left open", "deferred", "nobody has", "stays open", and the rest — and by
reading every "What it does not decide" section in `doc/design/decisions/`. A
deferral phrased in words that search did not cover is still out there, and the
only honest way to find it is to read the tree rather than grep it.

That each entry's owning package is really absent rather than merely unnamed.
The check was a search of `veneer:doc/IMPLEMENTATION-PLAN.md` for each item's
vocabulary. A package whose text covers an item in different words would read
here as unowned when it is not.
