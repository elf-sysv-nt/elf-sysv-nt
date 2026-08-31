# DR-0037 — the linking exception carries forward, as accepted practice

Status: accepted
Date: 2026-08-30
Deciding: the operator
Proposal: none; taken against the practice of the existing Cygwin forks,
recorded in `doc/proposals/licensing-issue.md` as items 1 and 2

## What was decided

The Cygwin linking exception travels with this modified library, and the
repository says so. Executables linking `elfsysv1.dll` are conveyed under it
the way executables linking `cygwin1.dll` are. The repository reproduces the
upstream exception verbatim — the text as Red Hat publishes it, unaltered —
and adds no wording of its own, which keeps faith with DR-0004's rule that no
engineer here drafts licence text.

This answers the two questions DR-0004 reserved. Item 1 of the licensing
docket asked whether the exception follows a modified version of the library;
it does, on the reading below. Item 2 asked how an LGPLv3 runtime serves a
GPLv2-only userland; the exception is the answer, which is what it exists
for. DR-0004 is not edited — this record points back at it, per the
append-only rule.

## The reading

Under GPLv3 section 7, incorporated by LGPLv3, an additional permission
travels with the work unless a conveyor removes it. Nothing in the
exception's text terminates it on modification. The counter-reading was
examined and is recorded in the docket: the grant names `libcygwin.a`,
`crt0.o` and `gcrt0.o` rather than the library in the abstract, and section 7
lets a licensee remove permissions but add them only over material of the
licensee's own copyright. Those points are real and unresolved as law. What
resolves them here is practice.

## The practice

MSYS2's runtime is a modified Cygwin library — their own description is "a
friendly fork of Cygwin" — and their tree ships `winsup/CYGWIN_LICENSE`
carrying the exception verbatim, still granting in the copyright holders'
voice, still naming `libcygwin.a`, with no special permission from Red Hat
sought or received. Git for Windows ships `msys-2.0.dll` beneath git, and git
is GPLv2-only: that distribution is lawful only if the exception follows the
modified library, and it has run at enormous scale for a decade without
objection from the copyright holder. The reading this record adopts is the
one the ecosystem already operates on. This repository does what MSYS2 does,
no more.

Checked 2026-08-30: `winsup/CYGWIN_LICENSE` at msys2-runtime tag
`msys2-3.6.4`, and https://cygwin.com/licensing.html for the upstream text.

## What this retires

The email drafted for the Cygwin mailing list
(`doc/proposals/licensing-email-draft.md`) is retired unsent; asking
upstream to confirm settled practice invited doubt where the ecosystem shows
none. The operator may still put the question to counsel or to Red Hat on
the operator's own timing, and an answer that contradicts this record
reopens it by a new record, not an edit.

## Not verified

That Red Hat shares this reading. Practice is acquiescence, not
confirmation; no written statement from the copyright holder addresses a
fork's carriage of the exception, and none was sought.

That MSYS2 or Git for Windows ever received private assurances. Their public
trees show none, which is what was checked.
