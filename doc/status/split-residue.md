# What the split left, and what is still open

Written 2026-09-05, and rewritten the same day when the work it described was
finished. It began as a count of what the repository split left dangling: 106
broken path citations, 24 citations to records that went with the veneer arc,
and two checkers that could not pass. All of that is closed. This file now
records what is genuinely open, so that it claims the present rather than a
morning.

## Closed

Every checker passes. `check-doc-refs`, `check-design-links`, `check-roots`,
`check-worknote-refs`, `check-conflict-markers`, `check-suites` and
`check-substrate-line` are green, the substrate conformance suite passes 9/9
against the mock and against N, and `core/run.sh` meets criterion 1's minimum
against the Rocky 8 oracle.

Citations into the veneer sibling are marked `veneer:`, which says which
repository a path means. Governing documents dropped the records that left
their Settled-by lines, three sections settled by nothing else were removed,
and DR-0097 ratified 0011 and retired eleven records in place. `Architecture.md`
was rewritten: the document that stood here described a veneer over a re-faced
Cygwin, down to Cygwin's own `_cygtls` reservation, and no part of it was
present-tense true of this tree.

Four defects surfaced along the way and were fixed at the root rather than
worked around. `spike/versioned-libc` had been dropped although two carried
spikes execute its unpacker. `t3-regen.sh` chose a transcript by mtime, which a
fresh clone flattens, so it silently diffed against a superseded one. The core
read every syscall argument as half a register, because `long` is 32 bits on
the host compiler. And the Phase 1 oracle ran `/bin/echo`, so it could not fail.

## Open

**The core is one syscall pair deep.** `write` and `exit_group` work; every
other number returns `-ENOSYS`. 0011 § 18 has seventeen more phases, and the
remainder of criterion 1 — `/proc/self/maps` and the vDSO — is untouched and
not faked.

**Substrate H is unbuilt.** Every one of its nine calls is backed by a landed
spike, so it is unblocked rather than unproven; the operator's steer put the
core on N first.

**Two spikes do not regenerate,** and neither is the split's doing.
`fs-base-persistence` flaps — a different case fails each run, identically in
the veneer checkout — which is the instrument bending under load rather than a
finding that moved; it wants a rerun on a quiet machine. `triple-fidelity`
loses one progress line from an otherwise identical transcript, and has done
since before the split.

**The 4 KB-against-64 KB arena rule** is a proposal-level call the operator has
not taken. The design builds against the documented 64 KB invariant and probes
for the finer one this host actually allows.

**Nothing has been pushed.** No remote exists for this repository, and the
publish-gate survey was acted on rather than merely counted: the paths are
parameterised under DR-0096, the commit history carries one author, and the
host name was ruled inconsequential.
