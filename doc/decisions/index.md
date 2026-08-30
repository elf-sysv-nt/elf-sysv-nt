# Decisions

One settlement per file, numbered in the order they were taken, never
renumbered. A record is append-only once filed: reversing one means writing a
new record that points back at it, which keeps the reasoning that was live at
the time from being quietly edited into the reasoning that is live now.

This index is one-to-one with the files beside it. A record without a row here
is a record nobody will find.

| # | Decision | Status | Proposal |
|---|---|---|---|
| [0001](0001-target-triple.md) | The target triple is `x86_64-elfsysvnt-linux-gnu` | accepted 2026-08-29 | 0001 |
| [0002](0002-el8-source-acquisition.md) | el8 source comes from Rocky 8.10 and lives outside the repository | accepted 2026-08-29 | 0001 |
| [0003](0003-tls-model.md) | The TLS model is a runtime-owned thread pointer through `%gs`, carrier C3 | accepted 2026-08-29 | 0002 |
| [0004](0004-license.md) | The licence is LGPLv3 or later, inherited from Cygwin's `winsup` | accepted 2026-08-29 | none |
| [0005](0005-bounded-linux-claim.md) | The `linux` field is a bounded claim, not a lie; DR-0001 stands | accepted 2026-08-29 | 0004 |
| [0006](0006-red-zone-direction.md) | The red zone is repaired at the delivery site; `-mno-red-zone` is scaffolding | accepted 2026-08-29 | none |
| [0007](0007-runtime-base-version.md) | The runtime is based on Cygwin 3.6.10 (`newlib-cygwin` b11613e47), not the pinned 3.0.7 | accepted 2026-08-30 | none |
| [0008](0008-mmap-granule-protection.md) | Segment mapping goes through the runtime's `mmap`, one region per object, protection at the host granule; a granule-sharing object is refused | accepted 2026-08-30 | none |
| [0009](0009-down-call-wrapper-convention.md) | The down-call wrapper is a signature-agnostic `ms_abi` tail jump; translation lands at the call site | accepted 2026-08-30 | none |

## What earns a record

Anything a different engineer would want the reasoning for six months on,
whichever route the change took. That is a lower bar than it sounds, and it is
deliberately lower than the bar for a proposal: a change can be cheap to undo
and still leave a question behind it worth answering once.

The three reservations in `AGENTS.md` each end in a record by construction.
