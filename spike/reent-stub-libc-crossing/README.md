# reent-stub-libc-crossing -- a host->faced libc call crosses a System V thunk

A rung of WP-56's `reent-tls-bringup` road (`acceptance/to-green.tsv`, item 1
of `acceptance/reent/README.md`). `spike/reent-stub-realproc-window` measured
that an ordinary Microsoft-ABI host call into the faced System V libc does not
cross (`ms_abi_libc_call_crosses=no`), while `cygwin_internal` reached through
an explicit `sysv_abi` bridge does. `acceptance/reent/stub-realproc.md` left the
open half of item 1 as a "bounded choice about how the stub reaches libc --
host-safe calls only for its own work, or the faced libc through the System V
crossing the ELF world already uses". This spike measures whether that second
route carries a general libc call, and splits it so the answer localizes the
obstacle.

## Findings, reproduced (`measure.sh`, 2026-09-01)

    startup_reached_main=yes
    sysv_thunk_reentfree_call_crosses=yes
    sysv_thunk_stdio_call_crosses=yes

The probe is the real-process shape the window spike relinks -- `-nostdlib`
against the WP-26 `crt0.o` and `-lcygwin`, with the same local `cygwin_internal`
bridge so `_dll_crt0` completes and control reaches `main`. Past startup it
reaches the faced libc twice, each through a `sysv_abi` function pointer
resolved from `elfsysv1.dll`'s PE export directory -- the shape the bridge
itself uses:

  - `strlen("abcd")` returns `4` across the thunk (`sysv_thunk_reentfree_call_crosses=yes`).
    A reent-free leaf, so this is the thunk mechanism alone: a host->faced libc
    call marshalled Microsoft->System V crosses and returns its value.

  - `puts` emits its line across the thunk (`sysv_thunk_stdio_call_crosses=yes`).
    A reent-consuming stdio body: it consults the reent `_dll_crt0` brought up
    during the real-process startup, and it crosses too.

The finding is stable across reruns. The very first automated run reported
`startup_reached_main=no`; rerun alone it reads `yes` every time -- the faced
runtime's known host-pty wedge on a first detached invocation, the load
artifact the reproducible-spike contract says to rerun before believing, not a
variable phenomenon.

## What this settles, and what it does not

It settles item 1's open half on the empirical axis: a host stub can reach the
faced libc through a per-call `sysv_abi` thunk, and the boundary
`reent-stub-realproc-window` found is the ABI *direction* (Microsoft-style into a
System V body), not reent bring-up. Once the real-process startup brings the
reent up the sanctioned way, even a reent-consuming stdio body crosses a plain
thunk. So the "bounded choice" `stub-realproc.md` names is not blocked on a
missing mechanism; routing the stub's own libc use through `sysv_abi` thunks is
a viable route, reent-consuming calls included.

It does not flip the `to-green.tsv` `reent-tls-bringup` signal. DR-0060 asks for
the reent's positive result reached *across the loader crossing* -- the loader
driving a reent-consuming ELF image -- not in a real-process probe, however
faithful its startup shape. This is a probe: a PE image that loads the faced
DLL through `crt0`, not the loader entering an ELF image through `enter.S`. The
reent-bearing ELF runtime the crossing resolves against is still the WP-53
`libc.so.6` veneer (item 2), and the reent-consuming ELF body entered through
the crossing is still item 3. This spike removes the ABI-boundary doubt that sat
under both.

## Reproducing

`measure.sh` builds `libc-crossing-probe.c` in the real-process shape against the
WP-26 `crt0.o` and the faced `-lcygwin`, runs it detached via `cmd` with stdin
from `NUL` (the faced runtime wedges on a host pty), and reads the markers. All
markers report through `kernel32` (native Microsoft ABI), so a marker is never
itself a crossing; only the two `sysv_abi` calls are. It SKIPs (verdict yes,
exit 0) when the faced DLL or the WP-26 build tree are absent, both uncommitted
build products, as the other real-process spikes do. Registered in
`test/spike-regen.tsv`.
