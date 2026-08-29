# DR-0004 — the licence is LGPLv3 or later

Status: accepted, with two questions reserved for counsel
Date: 2026-08-29
Deciding: the operator
Proposal: none; taken directly against Cygwin's licensing terms

## What was decided

The repository is licensed LGPLv3 or later. `COPYING.LESSER` carries LGPLv3 and
`COPYING` carries GPLv3, which is the pair LGPLv3 requires of a project that uses it, and
`doc/licensing.md` states the position in one page.

Most of this was not a choice. The rest of the record says which part was.

## Why the runtime settles it

Cygwin's API library, everything under `winsup/`, is LGPLv3 or later. Red Hat
moved it there in 2016 from GPLv3-with-exception and retired the commercial
buyout at the same time.

`elfsysv1.dll` is that library rebuilt with a different export face. `AGENTS.md`
puts it plainly — this project re-faces the DLL rather than replacing what is
behind it — and a re-faced library is a modified version of it. So LGPLv3+
attaches to the largest component in the program by derivation rather than by
election, and a repository licensed anything else would need relicensing the
moment WP-2x brings `winsup` source into the tree.

Cygwin's linking exception does not reach it, and the exception says so itself.
It grants permission to link `libcygwin.a`, `crt0.o` and `gcrt0.o` with
independent modules and convey the result under terms of your choice, and it
defines an independent module as one *not itself based on the Cygwin library*.
A re-faced Cygwin library is the excluded case by construction.

## The part that was a choice

Whether to license per component, as Cygwin does — LGPLv3 for `winsup`, GPLv3
for `utils` and `lsaauth`, upstream terms for everything packaged — or to put
one licence over the tree.

One licence, for now. The tree today holds documents, spike probes, patches and
tooling, and none of it is Cygwin-derived; per-component licensing would be
bookkeeping against a distinction that does not yet exist. When the loader and
the veneer are written they may reasonably be permissive, since both are
written from specification rather than lifted, and musl, the loader's working
model, is MIT rather than copyleft. That is a decision for whoever writes them,
made against a tree that by then has the distinction in it. Reversing this one
costs a relicensing of files whose authors are all in one place.

## What it costs, and this is the sharp edge

LGPLv3 cannot be combined with GPLv2-only programs. That incompatibility is why
glibc stayed at LGPL-2.1 rather than moving to version 3, and it lands directly
on this program's reason for existing: el8 ships GPLv2-only software, and a
`libc.so.6` veneering into an LGPLv3 runtime is a library that some of the very
packages this project exists to run cannot lawfully link.

Cygwin lives with the same shape and answers it with the exception, which waives
LGPLv3 section 4 for the linked executable and lets it be conveyed under terms
of the linker's choosing. Whether that answer survives into a re-faced
derivative — whether this project may go on granting an exception it received —
is the first of the two questions below.

Nothing in phase 1 depends on the answer. The first binary shipped to anyone
does.

## Reserved for counsel

Neither is an engineering question and neither should be answered by an
engineer, including by the one who wrote this file.

Whether a modified Cygwin library may carry Cygwin's linking exception forward,
and in what wording. Until that is answered this repository grants no exception
of its own; it states the intent to carry one and stops there, because inventing
licence text is how a project acquires a term nobody can interpret.

Whether an LGPLv3 runtime beneath a GPLv2-only program is a conflict in the
combination this project actually ships, given that the runtime is a shared
library reached through a veneer rather than a static archive. The general
answer is not in dispute; how it applies here turns on facts about the linkage
that WP-53 has not yet fixed.

## When to reopen this

When either reserved question comes back, if the answer makes LGPLv3+ the wrong
frame. When the loader or the veneer is written and a permissive licence for
that component is wanted, which is an addition rather than a reversal. Or if
Cygwin relicenses again, which it has done once.

Reopening means a new record pointing back at this one. Do not edit this one.

## Where it is written down

`COPYING` and `COPYING.LESSER` at the root, with `doc/licensing.md` beside this
record. `AGENTS.md`, under the licensing convention. `README.md`, in a section
at the end.
