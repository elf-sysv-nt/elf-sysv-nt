# DR-0095 — the licence is chosen, not inherited

Status: accepted
Date: 2026-09-05
Deciding: the operator
Proposal: 0011

## What was decided

The repository is licensed LGPL-2.1-or-later, and the licence is a choice made
here rather than an obligation carried in from anything this tree derives from.
`COPYING.LESSER` at the root is the text. Two earlier records, DR-0004 and
DR-0037, settled the licence for the veneer arc; neither carries into this
repository, and neither is amended. They stand where they were taken, in the
sibling `elf-sysv-nt--veneer`, describing a tree this one shares no code with.

## Why

DR-0004 reasoned by derivation. The veneer rebuilt Cygwin's API library behind
a different export face, so LGPLv3-or-later attached to the largest component
in the tree by inheritance, and the record said as much: the licence was not
picked, it arrived. Proposal 0011 dissolves that inheritance. A kernel at the
syscall boundary takes no code from `winsup`; the borrowed list is of ideas and
on-disk formats, which is reading rather than lifting, the distinction DR-0074
already draws and already checks. Nothing here is a modified version of
anything under LGPLv3, so nothing obliges the tree to stay there.

That leaves an actual choice, and the choice is the userland's. Every el8
program this kernel will run links glibc without a moment's thought about it,
and glibc is LGPL-2.1-or-later. Matching that is the version a packager,
a distribution, and a corporate reviewer have all already cleared: the "or
later" keeps the door to v3 open for anyone who wants it, while the floor at
2.1 means nothing here is stricter than the C library sitting on top of it.
Picking a permissive licence instead would have been defensible, and it was on
the table; it loses the one property worth keeping, which is that changes to
the kernel come back.

## Consequences

The root carries `COPYING.LESSER` and no `COPYING`: LGPL-2.1 is written against
GPLv2 by reference, not by requiring its text alongside. `doc/design/licensing.md`
states the licence and cites this record. Source files that grow a header cite
LGPL-2.1-or-later.

The reading rule stands unchanged. What the kernel reads while being written is
GPL in places — LTP, the Linux sources — and DR-0074's check is what separates
reading from lifting. Where a body's shape came from a GPL file rather than
from a manual page, the record says so and the body is rewritten from the page.

Settled by: the ladder at tier 4. Correctness, reliability and robustness do
not discriminate between a copyleft and a permissive licence for a tree with no
inherited obligation. What discriminates is that the proven choice for a
Linux-personality kernel under a glibc userland is glibc's own licence, which a
downstream integrator has already reasoned about.
