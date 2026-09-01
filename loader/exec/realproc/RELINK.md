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

## What stays deferred

Item 1's scope here is the version path -- the one reent-consuming body the
empirical phase composed end to end. The stub's other libc use (the Windows
placement path, the `fprintf` diagnostics, `slurp`'s file I/O) is not rerouted
in `stub.c` yet; it is not on the `--version` path and not needed for it.

The formatting half of that remaining use now has its foundation, though:
`realproc-fmt.c`'s `rp_snprintf` formats host-side over the conversions the
stub prints, certified against the platform `snprintf`, so the capstone relink
can turn `stub.c`'s `fprintf`/`printf` diagnostics into a host-side format
followed by the existing `rp_puts` crossing, rather than a `va_list` across the
ABI boundary. Adding the primitive left `stub.c` untouched -- the plain-PE
`stub.o` is cmp-equal before and after -- so it does not itself change the
program the WP-41 exec-* certifications drive.

Item 3 -- a reent-consuming ELF body reached across the loader, the
`to-green.tsv` `reent-tls-bringup` signal -- stays deferred behind the WP-53
`libc.so.6` veneer's forwarding bodies.
