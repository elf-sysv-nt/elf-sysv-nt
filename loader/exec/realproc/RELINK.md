# Relink of loader/exec/stub.c against the real-process layer

WP-56 reent-tls-bringup, item 1, implementing step. DR-0066 closed the empirical
phase; `loader/exec/realproc/` is the certified foundation. This is the relink
that wires `stub.c` to it through the `realproc.h` seam.

## What the seam carries

`stub.c` includes `realproc.h` and routes the libc operations the layer covers
through the `RP_*` macros: its option parsing -- `strcmp`, `strncmp`, `strtoull`
-- and its `--version` output, which prints `RELEASE`, a reent-consuming stdio
body.

Without `ELFSYSV_REALPROC` the seam is the identity. Under it the same source
links the layer's freestanding primitives for the parsing and its `sysv_abi`
`puts` thunk for the output, the two routes DR-0066 measured.

## The non-regression proof

The WP-41 exec-* certifications drive the plain-PE build, and item 1's bar is
that they do not regress. Two facts hold that bar, measured not asserted:

  - The parsing rerouting is object-code equal. Compiling `stub.c` plain before
    and after the `RP_STRCMP`/`RP_STRNCMP`/`RP_STRTOULL` change produced a
    byte-for-byte identical `stub.o` (`cmp`), because each macro expands to the
    call it replaced. So that half of the seam cannot change the plain program.

  - The `--version` output is behaviourally equal. `puts(RELEASE)` emits the
    same bytes as the former `printf("%s\n", RELEASE)`; the built stub's
    `--version`, `-V`, `--help`, unknown-option, and no-argument paths were run
    and are unchanged.

  - The full `loader/exec/t/run.sh` passes on the relinked tree: unit, the
    200k-case fuzz, when, and every exec-* check including `dyn-cross` and
    `dyn-init`.

## The dry-run report path

The capstone `realproc-fmt.c` foreshadowed has now landed for the one diagnostic
path that must cross when the stub runs in the real-process shape: the `--dry-run`
report. Its nine `printf` lines -- `stub_window_base`, the map and entry
addresses, `stub_exec_kind`, `stub_runtime_base`, the stack pointer and argc, and
the terminal `stub_result=ready` -- went through a `report()` helper that formats
the line host-side with `RP_SNPRINTF` and emits it through `RP_PUTS`, which
supplies the trailing newline the format now drops. This is the shape `realproc.h`
was built for: freestanding formatting, no `va_list` across the ABI boundary, and
only the finished bytes crossing through the `sysv_abi` puts thunk.

The report is the diagnostic the loader's exec-* checks and the crossing specimens
read, so its rerouting is the report path a real-process stub actually needs. In
the plain-PE build the seam is the identity, so `report()` is `snprintf` into a
128-byte buffer followed by `puts`: the same bytes the former
`printf("...
")` emitted. The non-regression bar is behavioural, not object
identity, because the output rewrite changes the object code by construction (it
did not for the parsing rerouting, which expands to the same call). The full
`loader/exec/t/run.sh` -- unit, the 200k-case fuzz, when, and every exec-* check
including `dyn-cross` and `dyn-init`, which run the built stub and read
`stub_result=ready` -- passes on the relinked tree.

## What stays deferred

The stub's remaining libc use is not on a path that must cross for the
`reent-tls-bringup` signal, and stays as it is: the stderr diagnostics
(`say`, `refuse`, `usage`, and the unknown-option and no-argument messages),
which write to `stderr` rather than the `stdout` the `rp_puts` thunk carries and
so want a separate stderr crossing before they reroute; the Windows placement
path; and `slurp`'s file I/O. None is on the `--version` or `--dry-run` path or
needed for it.

Item 3 -- a reent-consuming ELF body reached across the loader, the
`to-green.tsv` `reent-tls-bringup` signal -- stays deferred behind the WP-53
`libc.so.6` veneer's forwarding bodies.
