# DR-0067 — the real-process stub does its own work host-safe, and crosses only for output

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
item 1 of `acceptance/reent/README.md`.

## Context

DR-0066 closed item 1's empirical phase: the obstacle to a real-process host
stub is the Microsoft-to-System-V ABI boundary, not a window collision.
Startup's `cygwin_internal` crosses it, and so does the stub's own libc use --
a plain Microsoft-ABI call into the faced System V libc returns without
crossing (`spike/reent-stub-realproc-window`: `ms_abi_libc_call_crosses=no`).
DR-0066 left a bounded choice to the implementing step: whether the stub does
its own work with host-safe calls only, or reaches the faced libc for that work
through the System V crossing the ELF world already uses.
`spike/reent-stub-libc-crossing` proved the second route works, so the choice
was a design question, not an open measurement.

## Decision

The real-process stub does its own string and parsing work host-safe --
freestanding code that calls no libc, so it carries no crossing at all -- and
uses the System V crossing only where a result must come from the faced runtime,
which for the stub is output. `loader/exec/realproc/` is that layer:
`realproc-str.c` carries the freestanding primitives (the bases and forms
`stub.c` parses its options with, no locale and no errno), `realproc-cross.c`
carries the two crossings (the `cygwin_internal` startup bridge and a `sysv_abi`
`puts` thunk resolved from `elfsysv1.dll`), and `realproc.h` is the identity
seam for the plain-PE build the WP-41 exec-* certifications drive, so that path
is untouched.

The stub's own work is host-safe by construction -- it parses argument vectors
and formats diagnostics, none of it needing the faced runtime -- so confining
it to freestanding code costs nothing and keeps the ABI crossing to the one
direction that must cross. Routing that work through the System V crossing
instead would put a crossing on every option compare, buying nothing the
freestanding path does not already give.

## Consequences

The relink of `loader/exec/stub.c` itself, and its certification against the
WP-41 exec-* bar, remain item 1's finishing work; this layer is the certified
foundation it links, not the relink. `loader/exec/realproc/t/run.sh` certifies
the primitives natively, the identity seam's compile, and -- across the faced
runtime -- that `stub.c`'s `--version` path built from these units reaches main
and emits its `RELEASE` line, skipping when the build products are absent. The
`to-green.tsv` `reent-tls-bringup` signal is unchanged: it wires to a
reent-consuming body reached across the loader (item 3), not to this host-side
layer.
