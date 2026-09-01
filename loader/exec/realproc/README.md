# Real-process stub compatibility layer (WP-56 reent-tls-bringup, item 1)

`acceptance/reent/README.md` item 1 asks that the loader's PE host stub be
relinked in the real-process shape -- `-nostdlib` against the WP-26 `crt0.o` and
`-lcygwin`, so `_dll_crt0` brings the reent up -- without regressing the WP-41
exec-* certifications the plain-PE stub passes. DR-0066 closed the empirical
phase: the obstacle is the Microsoft-to-System-V ABI boundary, crossed both by
crt0 startup's `cygwin_internal` and by the stub's own libc use. This directory
is the certified foundation the implementing relink links, per the route DR of
this rung: the stub does its own work host-safe and crosses only for output.

  - `realproc.h` -- the seam. Without `ELFSYSV_REALPROC` the `RP_*` macros are
    the plain libc, so a translation unit that includes it is byte-for-byte the
    program it was; the plain-PE build the exec-* certifications drive is
    untouched. Under `ELFSYSV_REALPROC` the macros name the layer below.
  - `realproc-str.c` -- the stub's own string and option parsing, freestanding:
    pure over its inputs, no call into any libc, so no ABI crossing. Scope is
    what `stub.c` parses, not a general libc.
  - `realproc-fmt.c` -- the stub's own formatted output, freestanding for the
    same reason: `rp_vsnprintf` / `rp_snprintf` do the `printf`-family work
    host-side with no libc call, so only the finished bytes cross, through the
    `rp_puts` thunk. Scope is the conversions `stub.c` prints, not a general
    `printf`. Carrying a Microsoft-ABI `va_list` into the faced runtime's
    System V `vfprintf` would cross two register-save-area layouts that
    disagree, so the formatting stays host-side; DR-0066 draws that line.
  - `realproc-cross.c` -- the two crossings into the faced runtime: the
    `sysv_abi` `cygwin_internal` startup bridge (DR-0066's `-DBRIDGE` shape) and
    a `sysv_abi` `puts` thunk resolved from `elfsysv1.dll` for output.

`t/run.sh` certifies it: `unit` holds the freestanding primitives to known
results and to the platform libc, natively; `plain` compiles the identity seam;
`cross` builds `stub.c`'s `--version` path from these units in the real-process
shape and confirms it reaches main and emits its `RELEASE` line across the faced
runtime, skipping when the faced `elfsysv1.dll` or the WP-26 build tree -- both
uncommitted build products -- are absent.

The relink of `loader/exec/stub.c` itself against this layer has landed:
`stub.c` includes `realproc.h` and routes its option parsing and `--version`
output through the `RP_*` macros. `RELINK.md` records it and its proof against
the WP-41 exec-* bar -- object-code equality for the parsing, behavioural
equality for the output, and a clean `loader/exec/t/run.sh`. The `to-green.tsv`
`reent-tls-bringup` signal stays wired to a reent-consuming body reached across
the loader (item 3), not to this host-side layer.
