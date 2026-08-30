# Licensing, what was settled and what is still open

Written 2026-08-29 as a handoff. The licence question was taken far enough to
publish the repository and then deliberately tabled, so this says where it was
left and what the next session should pick up. It is unnumbered on purpose: the
numbered proposals each produced a decision, and this one is a docket rather
than an argument.

`doc/decisions/0004-license.md` is the decision. This does not restate it.

## What was done

The repository is LGPLv3 or later, and the reasoning is that the licence is
inherited rather than chosen. Cygwin's API library under `winsup/` is LGPLv3+,
this project rebuilds that library with a different export face, and a re-faced
library is a modified version of it rather than an independent module. Cygwin's
linking exception excludes exactly that case in its own words.

In the tree: `COPYING` and `COPYING.LESSER` at the root in GNU's layout for an
LGPL project, `doc/licensing.md` stating the position in a page, DR-0004 with
the reasoning, a paragraph in `AGENTS.md` so the next agent does not rediscover
it, and a section at the end of `README.md`. GitHub reports the repository as
LGPL-3.0.

Two corrections rode along. `IMPLEMENTATION-PLAN.md` had called glibc's
implementation GPL where it is LGPL-2.1-or-later; the conclusion that WP-36 is
written from Drepper's specification is unchanged, but the distinction now
carries weight. And `AGENTS.md`'s licensing paragraph gained musl, which is MIT
and is the loader's working model, since the paragraph listed only copyleft
prior art and that made the landscape look worse than it is.

The evidence is `https://cygwin.com/licensing.html`, read 2026-08-29, together
with Red Hat's 2016 announcement of the move from GPLv3-with-exception to
LGPLv3. Stale copies of the older terms circulate and say the Cygwin DLL is
GPL; work from cygwin.com rather than from a mirror.

## Open, and none of it is an engineer's to answer alone

### 1. Whether the linking exception carries forward

Cygwin grants permission to link `libcygwin.a`, `crt0.o` and `gcrt0.o` with
independent modules and convey the result under terms of the linker's choosing,
without complying with LGPLv3 section 4. That exception is what lets a program
under an otherwise incompatible licence link Cygwin at all.

`elfsysv1.dll` is a modified Cygwin library, so it does not receive the
exception as a linker. The question is whether this project may go on *granting*
an equivalent one to its own users, having received the code under terms that
carried it. Until that is answered the repository grants nothing, states the
intent, and stops.

This is the first thing to ask, because everything in item 2 depends on it.

### 2. LGPLv3 against a GPLv2-only userland

LGPLv3 and GPLv2-only do not combine. That incompatibility is why glibc stayed
at LGPL-2.1 rather than moving to version 3, and el8 ships GPLv2-only software,
which is the userland this project exists to run. A `libc.so.6` veneering into
an LGPLv3 runtime is a library that some of the packages in scope cannot
lawfully link.

The general rule is not in dispute. How it applies here turns on facts WP-53 has
not fixed yet: the runtime is a shared library reached through a veneer rather
than a static archive, and whether that is a Combined Work under section 4 or
something else depends on the shape of the linkage. Ask this one with the
veneer's design in hand rather than before it exists.

If both 1 and 2 come back badly, the consequence is not cosmetic. It would mean
the platform can run the el8 userland technically and not lawfully, which is a
different program, and DR-0004 should be reopened rather than patched.

### 3. What LGPLv3 section 4 obliges for a shipped runtime

Section 4 requires that a user be able to relink a Combined Work against a
modified version of the library. For a DLL loaded by a PE stub, with a loader
that maps ELF images itself, what satisfies that is not obvious and nobody has
looked. It bears on WP-63's installer as much as on the licence text.

### 4. Per-component licensing, later rather than now

DR-0004 put one licence over the tree because the tree holds no Cygwin-derived
code yet. The loader and the veneer are written from specification rather than
lifted, so both could reasonably be permissive and stay reusable outside this
project, the way Cygwin itself licenses `winsup` and `utils` differently. That
is a decision for whoever writes them, against a tree that by then has the
distinction in it. It is an addition to DR-0004 rather than a reversal.

### 5. Contribution and copyright, now that the repository is public

`doc/licensing.md` carries a single copyright holder. A public repository
invites patches, and nothing says whether they are taken under a DCO, a CLA, or
inbound-equals-outbound by default. The cheapest moment to decide is before the
first outside patch, not after.

### 6. Per-file notices for the patches

`toolchain/` holds patches against GNU config and against flac, and those carry
the licence of what they patch rather than this repository's. `doc/licensing.md`
says so in prose. Whether that is enough, or whether each patch wants a header,
is a small question that should be answered once rather than per patch.

## Not verified

Everything in the Open section, by construction.

That GitHub's LGPL-3.0 badge reflects a correct reading rather than a detector
matching a file. The detector matched `COPYING.LESSER`; it has no view on
whether the licence is the right one for what this tree will contain.

Whether any code in the tree today is already derivative of something copyleft.
The spike probes and the tooling were written here, the patches are derivative
of their targets by construction, and nothing has been lifted, but that is an
author's recollection rather than an audit.

## Resolution — F5 closed (2026-08-30)

On 2026-08-30 the operator, as the holder of the risk this docket describes,
directed that the licensing gate be considered complete for the design-gaps
adoption (finding F5 of `doc/design-gaps/proposal.md`). This is the dated,
operator-signed acceptance that finding asked for: F5 required either an
engagement noted with the questions as put or an explicit, dated decision to
proceed signed by the risk-holder, and this is the latter.

What this closes is the gate, not the questions. Items 1 through 6 above remain
the operator's to pursue with counsel on the operator's own timing, and
recording them keeps its value; DR-0004 is reopened by a new record if and when
counsel's answers require it, under the append-only rule. What changes is only
that the licensing questions stop being an unscheduled blocker on the work — the
adoption and the worker proceed.

No agent drafted or altered licence text in producing this record, per DR-0004.

— Philip Dye, operator
