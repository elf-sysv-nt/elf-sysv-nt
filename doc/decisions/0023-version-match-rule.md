# DR-0023 — the version matcher reproduces glibc's observable rule, from the spec

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-36 implementation, a defensible reading under the non-reserved
decision policy in AGENTS.md; the operator may ratify or reopen
Proposal: none; taken while building the version matcher

## What was decided

The matcher is written from the generic ABI and Drepper's account of the symbol
versioning records, not lifted from glibc's resolver, which is LGPL and assumes
a Linux kernel this platform does not have (DR-0000, DR-0004). What it
reproduces is glibc's *observable* resolution rule, and these are the readings
that were choices rather than transcription:

The name is the authority, the hash is a hint. A version is matched by its
name string in the object's own string table. The `vd_hash` and `vna_hash`
fields a linker writes are carried but not trusted for the match; a correct
matcher that ignored them would bind identically, and one that trusted them over
the name would mis-bind a hash collision.

A versioned reference with no exact match falls back to the unversioned base,
never to another node. When a reference names `GLIBC_2.14` and the candidate
carries a different named version, the candidate is rejected — except the
unversioned base definition (the reserved global index, version 1), which a
non-hidden reference accepts as a non-default binding. This is what lets a
program linked against a versioned symbol bind to an unversioned provider, and
what forbids it binding to the wrong version of a versioned one.

A definition node carries its predecessors, so a newer node implies the older.
Each `Elf64_Verdef`'s verdaux chain is the node's own name followed by its
parents; `elf_version_object_defines` matches any of them. So an object defining
`GLIBC_2.28` satisfies a requirement for `GLIBC_2.14`, which is what makes the
29-node el8 ladder answerable without listing every node at every requirement.

A weak requirement absent is tolerated; a non-weak one absent refuses the load.
`elf_version_check_needed` counts an unmet weak requirement and continues, and
refuses on the first unmet non-weak one with the string a real `ld.so` prints —
`version \`NAME' not found (required by CONSUMER)` — naming the library that was
supposed to provide it. Matching the message, not merely the verdict, is
deliberate: it is what a build log and a user's debugging both key on.

## What it does not decide

The lazy-versus-eager timing of when the check runs, and its placement relative
to relocation, are the loader's (WP-38's) to sequence. This record fixes what a
match and a refusal *are*, not when they happen.

## Where it is written down

`loader/version/elf_version.h` and `.c`, whose comments cite this record at the
choices above, and `loader/version/README.md`. The behaviour is held to the
fifteen checks in `loader/version/t/`.
