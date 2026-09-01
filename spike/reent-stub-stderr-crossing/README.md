# reent-stub-stderr-crossing (WP-56 reent-tls-bringup, item 1)

`acceptance/reent/RELINK.md` relinked the stub's `stdout` output through the
seam: `--version` and the `--dry-run` report cross the faced runtime on the
`rp_puts` thunk `spike/reent-stub-libc-crossing` measured. It left the stub's
`stderr` diagnostics deferred -- `say`, `refuse`, `usage`, and the unknown-option
and no-argument messages write to `stderr`, not the `stdout` `rp_puts` carries,
and "want a separate stderr crossing before they reroute". This spike measures
that crossing, so the reroute rests on a measurement rather than an assumption.

## The crux, confirmed

The `stdout` route is `puts`, a body that names no `FILE*`: the faced runtime
holds `stdout` internally and `puts` writes it. `stderr` has no such nullary
body. The obvious twin, `fputs(s, stderr)` or `fwrite(s, 1, n, stderr)`, needs
the faced runtime's `stderr` `FILE*`, and the faced `elfsysv1.dll` exports no
`stderr` (nor `stdout`) data symbol -- only the fd-taking bodies. The spike
records that as `stderr_file_export_present=no`. So a `FILE*`-based stderr write
has nothing to name, and the crossing must take an fd-2 body.

## The two fd-2 routes, both measured crossing

Two exported bodies write fd 2 without a `FILE*`, and the probe reaches each
through the same `sysv_abi` thunk shape the `cygwin_internal` startup bridge and
`rp_puts` use, resolved from the faced DLL's PE export directory. Both cross,
and the result reproduces (`results-2026-09-01.txt`):

  - `sysv_thunk_write_fd2_crosses=yes` -- `write(2, s, n)`, the raw fd-2 body.
    Non-variadic, no `FILE*`, no `va_list` crossing the ABI boundary. Its token
    reaches output and control survives the call.

  - `sysv_thunk_dprintf_fd2_crosses=yes` -- `dprintf(2, "%s", s)`, the fd-2
    formatted body. This one is variadic: it carries a Microsoft-ABI `va_list`
    into the faced runtime's System V vararg reader, the two-register-save-area
    disagreement DR-0066 draws a line at for `vfprintf`. It crosses here for a
    single trailing `%s` pointer, but that is the narrow case, not the general
    one, and it is not the route the reroute should take.

## What it implies for the reroute

`write` is the route: host-safe to prepare (the diagnostics format host-side
with `RP_SNPRINTF`/`RP_VSNPRINTF`, exactly as the `--dry-run` `report()` does),
non-variadic at the boundary, and free of the `FILE*` the faced runtime does not
export. So the stderr twin of `rp_puts` is a plain `sysv_abi` `write(2, ...)`
thunk, and the deferred diagnostics reroute by formatting their finished line
host-side and crossing it through that thunk. `dprintf` crossing is recorded as
a fact but deliberately not leaned on: keeping the `va_list` host-side is the
line DR-0066 holds, and `write` needs nothing across the boundary but bytes and
a length.

That the variadic route also crosses for the trailing-`%s` case is worth the
line it costs, because it bounds the earlier reading: the obstacle DR-0066 names
is the general `va_list` layout mismatch, not any variadic call at all, and a
later reader tempted to route stderr through `dprintf` should see that it works
yet is declined on the host-side-formatting principle, not because it faults.

## Reproducing

`bash measure.sh` (optionally `-o results-2026-09-01.txt`). It builds the probe
in the real-process shape against the WP-26 `crt0.o` and runs it detached
(`cmd`, stdin from `NUL`) against the faced `elfsysv1.dll`, as
`spike/reent-stub-libc-crossing` does. It SKIPs -- `verdict=yes`, exit 0 -- when
the faced DLL or the WP-26 build tree are absent, both uncommitted build
products. Registered in `test/spike-regen.tsv`.
