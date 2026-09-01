# Reroute of stub.c's stderr diagnostics through the write(2) crossing

WP-56 reent-tls-bringup, item 1, implementing step. `RELINK.md` routed the
stub's stdout output -- `--version` and the `--dry-run` report -- through the
`realproc.h` seam and left the stderr diagnostics deferred behind a measured
stderr crossing. `spike/reent-stub-stderr-crossing` measured that crossing;
this is the reroute it left as the next step.

## What the crossing is

The faced `elfsysv1.dll` exports no `stderr` `FILE*` (nor `stdout`), so the
`fputs(s, stderr)` twin of `rp_puts` has nothing to name: the crossing must take
an fd-2 body. The spike found `write(2, s, n)` crosses the faced runtime through
the same `sysv_abi` thunk shape the `cygwin_internal` startup bridge and
`rp_puts` use, non-variadic and carrying no `va_list` across the
Microsoft <-> System V boundary DR-0066 draws the line at. `rp_eputs` is that
thunk: `RP_EPUTS` is the identity `fputs`-to-stderr in the plain-PE build and,
under `ELFSYSV_REALPROC`, a `write(2, s, rp_strlen(s))` thunk resolved from
`elfsysv1.dll`'s export directory. The caller composes the finished line, the
`PROG` prefix and trailing newline included, host-side; `rp_eputs` writes it
verbatim.

## The five paths, rerouted

`say`, `refuse`, and the unknown-option and no-argument messages now format
through a single `ediag` helper -- `RP_SNPRINTF` the `PROG` prefix, `RP_VSNPRINTF`
the body, append the newline, `RP_EPUTS` the line -- the same host-side-formatting
shape `report()` uses for stdout. `usage` no longer takes a `FILE *`: it holds
its text without the trailing newline and emits it through `RP_PUTS` for stdout
(`--help`), which supplies the newline, or `RP_EPUTS` for stderr (an option
error), which takes it as a second write. The `mitigations_ok` reason strings
that feed `refuse` moved to `RP_SNPRINTF` too, so the rerouted diagnostic carries
host-formed bytes rather than a faced-libc `snprintf` that would return without
crossing.

## The non-regression proof

The WP-41 exec-* certifications drive the plain-PE build, and item 1's bar is
that they do not regress. The bar here is behavioural, not object identity: the
output rewrites change the object code by construction, as the `report()`
rerouting did. `loader/exec/t/run.sh` passes on the rerouted tree -- unit, the
200k-case fuzz, when, and every exec-* check including `dyn-cross` and
`dyn-init`, which run the built stub. In the plain-PE build the seam is the
identity, so each rerouted path emits the same bytes: `puts` and `fputs` write
what the former `fprintf`/`vfprintf`/`fputc` trio wrote, prefix and newline
placement preserved.

The crossing itself is certified live. `loader/exec/realproc/t/run.sh` gains an
`ecross` stage: `stderr-cross.c`, built real-process from the shipped units
(`realproc-str.c`, `realproc-cross.c`), emits a line through `RP_EPUTS` and the
run confirms it reaches fd 2 across the faced runtime with control surviving,
the same detached-`cmd` shape the `cross` stage and the reent-stub spikes use.
It SKIPs, `verdict=yes`, when the faced `elfsysv1.dll` or the WP-26 build tree
are absent, both uncommitted build products.

## What stays deferred

`slurp`'s file I/O (`fopen`/`fread` of the ELF image) and the Windows placement
path are the stub's remaining libc use; neither is a diagnostic and neither is
on a path the `reent-tls-bringup` signal turns on. Item 3 -- a reent-consuming
ELF body reached across the loader, the `to-green.tsv` signal itself -- stays
deferred behind the WP-53 `libc.so.6` veneer's forwarding bodies. This slice
closes item 1's diagnostic output; the row stays `-`.
