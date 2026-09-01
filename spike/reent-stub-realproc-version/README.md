# The real-process stub reaches its --version -- reent bring-up rung, item 1

`acceptance/reent/README.md` item 1 asks that the loader's PE host stub be
relinked in the real-process shape -- `-nostdlib` against the WP-26 `crt0.o`
and `-lcygwin`, so `_dll_crt0` brings the reent up the sanctioned way -- without
regressing the WP-41 exec-* certifications the plain-PE stub passes today. Three
spikes carried that from a plan to a measured obstacle and a proved fix, each in
isolation:

  - `spike/reent-stub-link` -- the stub links in the real-process shape
    (`realproc_stub_links=yes`) but the linked stub, run standalone, faults
    before it reaches even its `--version` path (`realproc_stub_reaches_version=no`).
  - `spike/reent-stub-realproc-window` -- the fault is the crt0 startup crossing,
    `_cygwin_crt0_common` calling `cygwin_internal` Microsoft-style into the
    faced runtime's System V veneer; a local `sysv_abi` bridge reaches `main`,
    while an ordinary Microsoft-ABI libc call still does not cross
    (`ms_abi_libc_call_crosses=no`).
  - `spike/reent-stub-libc-crossing` -- a libc call reached through an explicit
    `sysv_abi` thunk does cross, a reent-consuming stdio body (`puts`) included.

Each fix was proved on its own. This spike puts them together at the exact path
`reent-stub-link` found faulting -- `loader/exec/stub.c`'s `--version`, which is
`printf("%s\n", RELEASE)`, a reent-consuming stdio body -- and measures whether
the two together carry it.

## What it measures

One probe, `version-probe.c`, models the stub's `--version` path in miniature:
on `--version` it emits the `RELEASE` line (the literal `stub.c` carries) through
the faced libc, then a survival marker. Three build variants differ only in the
startup bridge and the ABI direction of that emit -- the isolation the two prior
spikes set up -- and the measure script reads one verdict from each:

  - `NO_BRIDGE` -> `startup_faults_without_bridge` -- no bridge; control never
    reaches the version path. Reproduces `reent-stub-link` at this path.
  - `PLAIN_PRINT` -> `version_print_plain_crosses` -- bridge in, `RELEASE`
    printed Microsoft-style; `main` is reached but the line does not cross.
    Reproduces the `realproc-window` `ms_abi` finding at the version path.
  - default -> `version_print_thunked_crosses` -- bridge in, `RELEASE` printed
    through a `sysv_abi` thunk; the line crosses and control survives.

All markers report through kernel32 (native Microsoft ABI, always safe), so a
marker is never itself a crossing; only the `RELEASE` line rides the faced libc.

## The finding, reproduced

`measure.sh` (2026-09-01) records `startup_faults_without_bridge=yes`,
`version_print_plain_crosses=no`, and `version_print_thunked_crosses=yes`. Read
together: once the startup bridge and a `sysv_abi`-thunked stdio path are in
place, a real-process stub reaches and completes the `--version` path
`reent-stub-link` found faulting. Item 1's remaining half -- how the stub reaches
libc -- has a demonstrated answer end to end: route the host-to-faced calls,
startup's `cygwin_internal` and the stub's own stdio alike, through the `sysv_abi`
crossing the ELF world already uses.

## What it is not

This is a probe, not the loader crossing, and not a change to `loader/exec/stub.c`.
It measures that the two fixes compose at the version path; applying them to the
real stub without regressing the WP-41 exec-* certifications is the
WP-41/WP-43-shaped work item 1 names, and item 3 -- a reent-consuming ELF
specimen entered through the loader crossing against the WP-53 `libc.so.6` veneer
-- stays deferred behind it. The `to-green.tsv` `reent-tls-bringup` signal is
unchanged: it wires to the positive result reached *across the loader*, not to
this host-side probe.

## Running it

    export PATH=/c/-/x-elfsysvnt/bin:$PATH
    ./measure.sh                 # findings to stdout
    ./measure.sh -o results-$(date +%F).txt

It SKIPs (verdict yes, exit 0) when the faced `elfsysv1.dll` or the WP-26 build
tree are absent, both being uncommitted build products. The faced runtime wedges
on a host pty, so each variant is run detached via `cmd` with stdin from `NUL`,
as the sibling `reent-stub-*` spikes do.
