# DR-0041 — the setjmp family takes a frameless face

Accepted 2026-08-31. Source: WP-27 milestone 7, the fault path.

## Context

Every sv2ms export crosses the face seam through a shape chosen by signature
class. The generic int face (`sv2ms-int.inc`) is sub, call, add, ret: it
builds a small Microsoft frame, calls the body, and returns through its own
frame. That shape is correct for a body that runs once and returns once,
which is every body on the surface except six.

The exceptions are the setjmp family — `setjmp`, `_setjmp`, `sigsetjmp`,
`longjmp`, `_longjmp`, `siglongjmp`. Their bodies (gendef's hand-written
assembly in the vendor tree) capture the caller's context literally: setjmp
stores `%rsp` as it stands and the return address at `(%rsp)`. Behind a
call-style face, what it captures is the face's frame, not the ELF caller's.
The face returns, its stack region is reused by the caller's later calls,
and the eventual `siglongjmp` resumes inside a dead face instance whose
caller-return slot now holds the return address of whichever call reused the
slot. The face's ret follows it and control lands after the wrong call site,
with siglongjmp's value in `%rax`. This is the long-standing rule that
setjmp must not be wrapped in an out-of-line wrapper, surfacing at the face
seam; WP-27's fault-path milestone met it directly, since SIGSEGV leaving by
`siglongjmp` is exactly a longjmp across reused stack.

## Decision

The family gets a face with no frame: `sv2ms-ctx.inc` shuffles the two
integer argument registers and jumps to the body. The body then sees the ELF
caller's true `%rsp` and return address, captures those, and the restoring
jump lands in the caller directly — no face instance alive on either path.

Membership is curated in `ctx.tsv`, not derived. The frameless shape is only
sound when the body promises never to touch its caller's shadow area,
because the jump hands it a System V stack with the caller's own frame where
a Microsoft caller would have left four spill slots. gendef's assembly keeps
that promise — it builds its own aligned argument space for the calls it
makes — but a compiled Microsoft body may home its argument registers in the
shadow area, and no header states which bodies do. A promise about a body's
code is a curation, so the table is small and each addition must be argued
from the body's source. `gen-ctx-faces.sh` emits the faces,
`gen-int-faces.sh` excludes the table's rows, and `t/ctx-face.sh` certifies
the shape and the semantic property, with a call-style control showing the
property is one the old shape really lacked.

## Consequences

The int class is 1309 generic faces and the ctx class is 6; the export
table's shape does not change, since both classes bind the same `__face_`
prefix at the `.din` seam.

The `ucontext` triple — `getcontext`, `setcontext`, `swapcontext` — has the
same capturing nature and stays on the call-style face for now, because its
bodies are compiled C holding no shadow-space promise, so neither shape is
right for them. An ELF program using ucontext across the face will misbehave
the way the control in `t/ctx-face.sh` does. They are deferred, not settled:
a written face that captures at the seam is the likely repair, and whoever
takes it should reopen this record. `vfork` also returns twice but through
fork's process-level copy, which duplicates the face frame intact into the
child, so the call-style face holds for it.
