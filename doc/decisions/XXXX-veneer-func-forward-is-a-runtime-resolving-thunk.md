# DR-XXXX — a veneer FUNC forward is a runtime-resolving thunk

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
item 2 of `acceptance/reent/README.md`.

## Context

`veneer/libc/generate.py` emitted a bare `ret` for every FUNC body. The surface
that rests on it — the soname, the node ladder, the symbol-to-node assignment —
is real, and the whole tree links against it, but a `ret` returns to its caller
having done nothing, so a program that calls one gets a wrong answer rather than
a diagnostic. Item 2 of the reent rung asks that a forward stop being a promise:
its body must reach the `elfsysv1.dll` export the classification names for it.

Two spikes measured why that body cannot be a link-time forward.
`spike/reent-veneer-body` showed a naive `jmp strtol@PLT` under the veneer's own
`.symver` self-binds: the linker resolves the unversioned reference to the
default-versioned definition the veneer itself provides, so the body jumps to
itself. The export lives in `elfsysv1.dll`'s PE export directory, which is not
an ELF dynamic symbol; the WP-27 crossing resolves it at run time from
`AT_BASE` (`runtime/face/t/elfcall.c`'s `pe_export` walk). `spike/reent-veneer-thunk`
then pinned the link-time shape of a body that reaches the export the crossing's
way: it names the target as data and resolves it at run time, holding no ELF
dependency on the faced name.

## Decision

A FUNC row whose disposition is `forward-same` or `forward-alias` is emitted as
a runtime-resolving thunk, the shape the second spike pinned. Three points fix
how, and each is a choice with an alternative that was rejected.

The body names its export as a `.rodata` string and reaches it at run time, not
as an ELF symbol. This is forced, not preferred: the export is a PE symbol the
ELF world cannot name, and any ELF reference to the faced name self-binds. The
`.rodata` name is the crossing ABI's own key.

The bodies share one hidden per-veneer resolver, not a copy each. The spike's
probe was a self-contained 40-byte body that inlined the walk. Emitting that per
symbol would put the whole PE-export walk in every one of ~1160 forwards.
Instead `resolver.c` carries the walk once, an `AT_BASE` constructor that finds
the faced base, and an argument-preserving cold trampoline; each generated body
is a 12-byte `lea name(%rip), %r11; jmp _elfsysv_thunk`. The trampoline
preserves the SysV integer and FP argument registers around the resolve call and
tail-jumps to the export with the arguments intact, so the cold path costs a
resolve per call but the hot bytes per symbol are minimal. All three resolver
symbols are hidden and stay out of `.dynsym`, so one veneer carries one resolver
that collides with no faced name.

IFUNC, `shim`, and `stub` bodies stay a bare `ret` and convert on their own
rungs. An IFUNC resolver runs during load-time relocation, before the
constructor brings the face base up, so a thunk-shaped IFUNC would resolve
against a null base; that ordering is the crossing's to settle, not a flag to
flip here. A `shim` needs a translation through WP-55's tables, not a forward. A
`stub` names no export. Folding any of these in would be a guess where the
tree has not yet measured the answer.

## Consequences

The reent rung's item 2 is met at link time: `veneer/libc/t/run-tests.sh` now
certifies, on the built `libc.so.6`, the four facts the spike pinned — a
versioned FUNC body with real code, no ELF self-import of the faced name, that
name in `.rodata`, and the resolver kept private. The `reent-tls-bringup` signal
in `acceptance/to-green.tsv` stays `-`, because item 3 — a reent-consuming body
returning `LONG_MAX`/`ERANGE` across the loader — needs the built `elfsysv1.dll`
face and a run, and remains deferred behind the WP-53 veneer. This decision is
the codegen half; it does not claim the run.
