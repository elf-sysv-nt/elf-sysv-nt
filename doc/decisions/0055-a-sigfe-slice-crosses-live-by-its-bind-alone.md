# DR-0055 — a SIGFE slice with no pure NOSIGFE row crosses live by its bind alone

Accepted 2026-09-01. Source: WP-56, the stdio slice's live crossing.

## Context

Every WP-56 live crossing before stdio exercised a real body. Each chose the
slice's NOSIGFE forwards -- the rows standing on no reent, locale, table or
kernel -- and called them through their generated thunks against the real
`elfsysv1.dll`: math's `fabs`, string's `ffs`, stdlib's `abs`, sockets's
`htonl`, locale's `toascii`, time's `difftime`, misc's `insque`, terminal's
`cfmakeraw`. The body ran, and its observable result either confirmed the
forward crossed value-preserving or, for wchar and terminal, refuted it and
named the shim the row needs.

stdio offers no such row. Of its 97 wired rows exactly one is marked NOSIGFE
in `runtime/exports/cygwin-exports.tsv` -- `cuserid` -- and `cuserid` is not a
pure function: its body reads the process's user identity through the cygheap,
state a freestanding specimen never establishes. Calling it would fault or
read uninitialised memory, exactly as calling `setlocale` would. The other 96
rows are SIGFE: their thunks want the signal-frame entry the specimen
deliberately compiles without, and the `printf`/`scanf`/`FILE` families behind
them stand on `_REENT` besides. `NOSIGFE` names the calling convention a thunk
needs, not whether the body behind it stands on its own -- locale's crossing
established that -- and stdio is where the two part company completely: even
locale kept one pure exception, and stdio keeps none.

## Decision

A slice whose rows are all SIGFE, or whose only NOSIGFE rows stand on process
state a freestanding harness cannot bring up, is crossed live by its bind
alone. The live crossing certifies what a freestanding harness can certify of
such a slice against a real DLL, and no more:

  - the bind resolves every row (`missing` 0) -- every name reaches an export;
  - every filled slot lands inside the DLL's mapped image span, so a resolved
    thunk tail-jumps into the real body region and not into unmapped space;
  - the resolver discriminates -- a real export resolves, an un-exported name
    and a junk name do not -- so the all-resolve result is a fact about the
    names and not the resolver handing back any address;
  - distinct names reach distinct bodies;
  - the bind is idempotent, per `DR-0049`'s contract, so a rebind after a
    runtime reload is a plain re-run.

The bodies of such a slice are left to the two bars that can reach them: the
per-slice differential (`diff-slice.sh`) on the pinned el8 image, over the
WP-T2 environment, for their observable glibc behaviour; and process bring-up
for their live NT behaviour. Neither is a freestanding harness's to give, so
neither gates the slice's live crossing.

## Consequences

stdio's live crossing (`veneer/wiring/t/live-stdio.sh`, thirteenth) calls no
body and passes on the five bind properties above. `bin/progress.py` and
`bin/build_status.py` count a slice crossed when its `live-<slice>.sh` is
present, and neither reads whether a body was exercised, so the bind-only
crossing marks stdio crossed exactly as the body-exercising crossings marked
their slices.

The SIGFE-heavy slices still uncrossed inherit the rule. `memory`, `signal`,
`process`, `identity`, `io-mux`, `threads`, `regex`, `syslog`, `sysv-ipc`,
`io` and `system` each carry few or no pure NOSIGFE rows; each will present its
own worklist to the same question, and where the answer is the same as stdio's
the crossing is the bind and the finding is that the bodies wait. This is not a
gap in coverage. It is where the freestanding-harness technique reaches its
limit and hands the bodies to the differential and to bring-up, which is where
they were always going to be certified.

This record does not weaken any slice's done-when. WP-56's per-slice bar is
still the differential against a real el8 userland; the live crossing was only
ever the added NT check, and for a SIGFE slice that added check is the bind.
