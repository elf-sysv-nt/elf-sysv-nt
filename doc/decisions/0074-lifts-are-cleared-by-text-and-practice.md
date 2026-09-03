# DR-0074 — a lift is cleared by licence text and recorded practice; LGPL-2.1-or-later is open

Status: provisional
Date: 2026-09-02
Deciding: the operator, in conversation on 2026-09-02; drafted for
ratification under the non-reserved decision policy, since licensing is a
reserved area and DR-0004 is the standing record
Proposal: none; taken against the licence-restriction analysis of 2026-09-02,
kept as a working note, and the open items of `doc/proposals/licensing-issue.md`

## What was decided

Two things, one general and one specific.

The general one: this project clears a lift on the licence text, the FSF's
published compatibility guidance, and recorded practice: a named project
that has done the same combination in public, at a ref somebody here read,
without objection from the copyright holder. Counsel is not in the loop and
will not be; the project has no budget for it, and the docket's standing
request for an engagement is withdrawn rather than left to rot. Every future
lift record carries a Precedent section shaped like DR-0037's, naming who
does it, where in their tree, the ref checked and the date, and a Not
verified section that says practice is acquiescence rather than
confirmation. That is the whole of the licence gate. A lift that cannot
produce a precedent is not blocked; it is recorded as resting on text alone,
which is what most lifts rest on everywhere.

The specific one: LGPL-2.1-or-later material may be taken into the shipped
runtime outright (`elfsysv1.dll`, the veneer, the loader, anything mapped
into a user's process). glibc is in this class, and so is most of the GNU
runtime. The obligation is the ordinary LGPL one and this tree's own licence
already meets it. What still rules glibc code out, where it is ruled out, is
coupling; the governing document names the coupling and not the licence.

## Why text settles the LGPL-2.1+ class

The "or later" clause is the whole argument. LGPL-2.1 section 13 lets a
recipient follow any later version, LGPLv3 is a later version, and this tree
is LGPLv3+. The FSF's compatibility matrix at
`https://www.gnu.org/licenses/gpl-faq.html#AllCompatibility`, read
2026-09-02, gives the same answer for code under "LGPLv2.1 or later" copied
into a project under LGPLv3: permitted, conveyed under LGPLv3. glibc's file
headers carry the clause; `veneer/include/` already stands on this footing
under DR-0010, and nothing in this record changes that footing, it widens
what stands on it.

The trap in the class is LGPL-2.1-only. Without the clause there is no path
to version 3, and the two do not combine. Read the file header, not the
project's reputation.

## Precedent

GNU libunistring is licensed "LGPLv3+ or GPLv2+" (its manual, Appendix C,
read 2026-09-02) and is assembled from gnulib modules, most of which are
LGPLv2.1-or-later; the FSF's own tooling rewrites the module headers to the
target licence on import. That is the copyright holder of the licence itself
operating the upgrade this record relies on.

elfutils' libraries, libelf and libdw and the backends, are dual GPLv2+/LGPLv3+
(`https://sourceware.org/elfutils/`, read 2026-09-02). Under the LGPLv3+ arm
they land exactly on this tree's licence, which makes a mature ELF reader
liftable without any version step at all, and makes elfutils a second
example of a GNU-adjacent project choosing LGPLv3+ as the home for
LGPL-descended code.

Bionic, for the neighbouring class: Android's libc vendors the kernel's uapi
headers verbatim under `libc/kernel/uapi/`, cleaned by script rather than
rewritten, under the Linux-syscall-note exception
(`platform/bionic`, `libc/kernel/README.md`, `refs/heads/main`, read
2026-09-02). That is the precedent for taking syscall numbers, errno values
and structure layouts from the kernel's own headers into a non-GPL runtime,
at Google's scale.

## What this closes in the docket

Item 4, per-component licensing, closes the other way round from how it was
posed. The loader and the veneer stay LGPLv3+ rather than going permissive,
because a permissive component could not absorb this class, and glibc is the
largest liftable body the project has. Reusability outside this project was
the argument for permissive; being able to lift glibc's relocation
processors, hash tables and TLS layout is worth more here than that.

Item 3, section-4 relinking, is answered by practice rather than analysis:
every LGPL library shipped as a Windows DLL — Cygwin's own among them —
satisfies it by conveying source and build instructions, and this project
does the same. Nobody ships a relink kit, and nobody is asked to.

Items 5 and 6 stand open; neither bears on lifting.

## Consequences

`AGENTS.md`'s licensing paragraph states the precedent rule and corrects one
entry: Blink is ISC (`jart/blink`, `LICENSE`, `master`, read 2026-09-02),
not GPL, and its Linux syscall layer is therefore in the permissive class
and open to the runtime. flinux, Qiling and QEMU's linux-user remain GPL and
remain out of the runtime, in for tooling, oracles and reading.

The working analysis stops being the only place the licence classes are written
down. Its table moves, in substance, into this record and `doc/licensing.md`;
the note stays as the working history behind them.

Each lift into the runtime still pays the mechanics `AGENTS.md` already
asks for: notices retained unmodified, a row in `doc/licensing.md`'s
third-party section, a pinned upstream ref, and the used / adapted /
written-from-specification declaration in the governing document. Where a
lift is adapted, the document says what changed.

## Not verified

That any of this is legally correct. It is an engineering reading of licence
texts and of the FSF's guidance, made by people who are not lawyers and who
have decided, for want of money, to proceed on that reading. Every claim
here is checkable against the cited texts; that is the most that is claimed.

That libunistring's gnulib modules are in fact LGPLv2.1+ at the ref built
from. The manual's licence appendix was read; the module headers were not,
and gnulib's `modules/*` files carry the per-module licence field that
would confirm it.

That the compatibility matrix says what this record reports. The FAQ page
was fetched and its index reached; the table body itself was not rendered
by the tool used, and the reading is from memory of the table, which has
been stable for years. Somebody should open it in a browser and check the
"LGPLv2.1 or later" row against the "LGPLv3" column.

That DR-0037's carry-forward reading holds. Everything about the runtime's
linkability inherits that uncertainty, and this record adds none of its own.
