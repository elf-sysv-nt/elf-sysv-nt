# reent-stub-realproc-window -- where the real-process stub relink faults, and why

A rung of WP-56's `reent-tls-bringup` road (`acceptance/to-green.tsv`, item 1
of `acceptance/reent/README.md`). `spike/reent-stub-link` relinks the loader's
PE host stub in the real-process shape -- `-nostdlib` against the WP-26 `crt0.o`
and `-lcygwin`, so `_dll_crt0` brings the reent up -- and finds it links but
faults before its `--version` path. `acceptance/reent/stub-realproc.md` recorded
the fault as an address collision: a "minimal non-PIE PE that adopts a
parent-reserved low window (DR-0028)" whose `0x400000` window `_dll_crt0`'s "own
low mappings" collide with "before `main`". This spike locates the fault with a
controlled before/after, and puts the cause elsewhere.

## Findings, reproduced (`measure.sh`, 2026-09-01)

    realproc_stub_image_base=0x0000000100400000
    startup_faults_without_bridge=yes
    startup_reached_with_bridge=yes
    ms_abi_libc_call_crosses=no

The stub links at `0x100400000`, the ordinary high Cygwin image base, not the
`0x400000` low window. The stub is not in the window it reserves: the window is
the ELF world's, held for a suspended child (DR-0028), and the `--version` path
reserves nothing. So there is no collision between the stub's image and the
window to be the fault.

The fault is the crt0 startup crossing. `_cygwin_crt0_common` calls
`cygwin_internal(CW_USER_DATA)` with the Microsoft ABI; the faced `elfsysv1.dll`
exports `cygwin_internal` as a System V veneer (the WP-27 crossing ABI). Reached
Microsoft-style, the System V body faults before `main`. Interposing one local
`cygwin_internal` that re-crosses the call -- the bridge `spike/reent-bringup`'s
real-process probe already carries -- reaches `main`. The single build flag
`-DBRIDGE` is the whole difference between a fault and an entry.

Past startup, one ordinary `printf` -- a Microsoft-ABI call into the faced
System V libc -- produces no output while control survives it: the `A` and `C`
markers print, the `printf`'s own `B` line does not. The stub's own code is host
Microsoft-ABI code, and every libc call it makes for its own work (`--version`'s
`printf`, the diagnostics' `fprintf`, `getopt`, `snprintf`) meets the same
boundary. Bridging one export does not lift it.

## What this settles

Item 1 is not the window/image-base reconciliation the account named. The
obstacle to a real-process stub of the faced runtime is the Microsoft <-> System
V ABI boundary at every host-to-faced-runtime call -- startup's `cygwin_internal`
first, then the stub's whole libc use. The decision this spike carries records
that and reframes item 1 accordingly; `acceptance/reent/stub-realproc.md` is
corrected to match. The plain-PE stub the WP-41 exec-* certifications drive is
untouched: it links against the unfaced Cygwin and speaks Microsoft to a
Microsoft runtime, and this rung builds the real-process link only as a separate
measurement.

`stub-abi-probe.c` is the smallest real-process image that reproduces the two
facts that matter -- a startup that faults on the crossing, and a host libc call
that does not cross -- built twice, with and without `-DBRIDGE`. The stub's own
code carries no dependency on it. `measure.sh` SKIPs (verdict yes, exit 0) when
the faced DLL or the WP-26 build tree are absent, both uncommitted build
products, as the other real-process spikes do. Registered in
`test/spike-regen.tsv`.
