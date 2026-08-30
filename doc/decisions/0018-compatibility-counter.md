# DR-0018 — the compatibility counter is Cygwin's, re-faced, enforced on the combined API and kept from the first release

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: the operator
Proposal: none; taken as WP-25 built the counter DR-0007 deferred

## What was decided

`elfsysv1.dll` versions its outward surface with three axes, inherited from
Cygwin rather than reinvented, and enforces backward compatibility on them at
load:

  - a **generation**, the digit in the name `elfsysv1`, re-facing Cygwin's
    `CYGWIN_VERSION_DLL_IDENTIFIER "cygwin1"`. It is the axis no counter
    reaches; a different digit is a different DLL a program never loads by
    accident. It starts at 1 and moves only for a break no backward-compatible
    counter could bridge.
  - an **API major and minor**, re-facing `CYGWIN_VERSION_API_MAJOR` and
    `CYGWIN_VERSION_API_MINOR`. Major is reserved for an incompatible change
    within a generation; minor is bumped additively for every export a program
    could depend on. The pair starts at `0.1`.

A program carries a stamp of the values it was built against —
`elfsysv_version_stamp`, re-facing the `api_major`/`api_minor`/`magic_biscuit`
fields Cygwin's crt0 writes into `per_process`. The runtime reads it at load and
refuses, with a diagnostic rather than a crash, when the program was built
against more than the runtime provides. This re-faces Cygwin's
`check_sanity_and_sync` (`dcrt0.cc`) at `newlib-cygwin` b11613e47.

Two points depart from Cygwin deliberately, and are the substance of this
record.

## The enforcement is on the combined pair, not the major alone

Cygwin's load-time refusal tests the major only: `if (p->api_major >
cygwin_version.api_major) api_fatal (...)`. The minor is left to compile-time
feature macros of the form `CYGWIN_VERSION_..._COMBINED >= N`, which the DLL
uses to decide behaviour for a program of a given combined version.

Here the load-time refusal reads the combined `major * 1000 + minor` and refuses
a program whose combined value exceeds the runtime's. The reason is that this
project's minor is where every additive change to the exported surface lands,
and a program built after one genuinely needs a runtime that carries it. Testing
the major alone would let a program built against `0.7` load against a `0.5`
runtime and fail at the first missing export instead of at the door, which is
the failure the milestone's diagnostic exists to prevent. Refusing on the
combined pair is the direct reading of WP-25's done-condition, which is stated
in minors: "a program built against a lower minor runs against a higher one, and
the reverse is refused with a diagnostic rather than a crash."

This does not weaken the major axis. A major bump still refuses every lower
major, because the combined number carries it (`1.0` is `1000`, above any `0.x`).
It adds the minor to the same comparison rather than replacing the major with it.

## The counter starts at the first release, not the first break

Cygwin's counter is a long retrospective list because it was kept from early in a
long history. This project stamps `0.1` at the first release. Retrofitting a
counter after the fact means guessing which already-shipped binaries predate
which change, and the binaries do not carry the answer, so the guess cannot be
made honestly. Starting at the first release means every object the runtime ever
sees carries a stamp that means something. `runtime/version/CHANGELOG.md` records
`0.1` as the baseline and governs every later bump.

## What it does not decide

Where the program's stamp is written and where the runtime reads it. This record
fixes the counter, the stamp's shape, and the check; the crt0 that emits
`ELFSYSV_VERSION_STAMP_INIT` into a program's image and the loader site that
reads it before handing over control are the startup and loader packages' work,
against this shape. The test stamps its objects directly to stand in for that
emission.

The per-symbol veneer mechanics. Which exports a given minor adds, and whether a
symbol is a forward, a shim, or a stub, is WP-52's and the changelog's, not this
record's.

## What it costs to reverse

Cheap while the counter is `0.1` and nothing has been stamped against a later
value; the comparison is one function and the stamp one struct. It grows dearer
as programs are built and stamped against successive minors, because the meaning
of "backward compatible" they were promised is the combined rule this record
fixes. Reversing to a major-only comparison later would reclassify which of
those programs the runtime still accepts.

Reversing this is a new record pointing back here, not an edit to this one.

## Where it is written down

`runtime/version/elfsysv-version.h`, the counters and the stamp.
`runtime/version/compat.c` and `compat.h`, the check.
`runtime/version/CHANGELOG.md`, the changelog discipline and the `0.1` baseline.
`runtime/version/README.md`, which names this record and reads the Cygwin
mechanism against b11613e47. `doc/IMPLEMENTATION-PLAN.md`, WP-25, marked
delivered.
