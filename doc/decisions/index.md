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

## What earns a record

Anything a different engineer would want the reasoning for six months on,
whichever route the change took. That is a lower bar than it sounds, and it is
deliberately lower than the bar for a proposal: a change can be cheap to undo
and still leave a question behind it worth answering once.

The three reservations in `AGENTS.md` each end in a record by construction.
