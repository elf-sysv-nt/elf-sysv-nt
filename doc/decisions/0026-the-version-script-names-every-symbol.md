# DR-0026 — the generated version script names every symbol

Status: accepted
Date: 2026-08-30
Deciding: the WP-53 agent, on a measurement rather than a preference
Proposal: none; taken when WP-53 first linked libc.so.6

## What was decided

`veneer/libc/generate.py` emits a linker version script that lists all 2329
exported symbols by name, each under the version node the map assigns it, with
`local: *` under the first node. It does not emit a script of bare node
declarations and leave the symbol-to-node assignment to the `.symver`
directives in the assembly.

The assembly still carries a `.symver` for every symbol, so each fact is stated
twice, in two files, from one source. That duplication is deliberate and is
explained below.

## Why

The first version of the generator wrote the shorter script: 29 empty nodes in
ladder order, `local: *` under the first, and one `.symver` per symbol in the
assembly to say which node each belongs to. It assembled and it linked. It
produced a library with 566 dynamic symbols where 2329 were emitted, and the
compat binding of `memcpy` was among the ones that vanished.

The reason is that the version script decides two separate things and the
`.symver` directive decides only one. `.symver` names the version node a symbol
is bound to. The script's patterns decide whether a symbol is exported at all,
and that decision is taken by matching the symbol's base name — `memcpy`, not
`memcpy@GLIBC_2.2.5` — against the script's `global` and `local` patterns. A
bare `local: *` matches every base name, so it localized most of what the
assembly had just versioned. Which ones survived is not worth characterizing;
the point is that the two mechanisms are not interchangeable and the failure is
silent. The link succeeded. The file was three quarters empty.

Naming every symbol in the script is what glibc's own build does, through its
per-library `Versions` files, and for this reason. A symbol carried at two
nodes is listed at both, which is how the compat binding and the default
binding stay two symbols rather than one.

## What it costs

The script is 2449 lines instead of about ninety, and every exported name is in two
generated files. Both are generated from the same two inputs in the same pass,
so they cannot disagree with each other without disagreeing with the map first,
and `t/run-tests.sh` reads the linked file back and compares its whole export
set against the map. A drift between the assembly and the script shows up there
as a missing or an extra symbol, not as a smaller library that still links.

The alternative to the duplication would be to drop the `.symver` directives
and let the script alone assign nodes. That does not work either: a script
cannot give one name two nodes, which is precisely what a compat binding is.

## What this does not decide

Nothing here concerns what the symbols do. Every body in the current library is
a `ret`, and `libc-forward.tsv` records which `elfsysv1.dll` export each entry
is to reach. Filling those in is later work; the surface this record is about
is what the loader, rpm and the linker see, and it is complete now.
