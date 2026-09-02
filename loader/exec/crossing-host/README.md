# The faced-runtime crossing host (WP-56 reent-tls-bringup, DR-0071 finishing step)

DR-0071 decided the acceptance crossing hosts the faced runtime as its own
process: a real process of `elfsysv1.dll`, brought up through the WP-26 `crt0`
`_dll_crt0` protocol so the faced runtime is the process's sole Cygwin runtime.
The image (bzip2) is mapped through that faced runtime's own `mmap` (the DR-0008
mapping) and entered inside the process, and `AT_BASE` carries the faced
runtime's own base -- laid down by its own startup -- so the veneer's forwarding
thunks resolve against a live face.

This directory holds that crossing host: the build recipe that links it in the
real-process shape, and the entry path that maps and enters the ELF image inside
it. It reuses the loader's certified map/enter units (`loader/map`, `enter.S`,
`dyn_exec.c`) and the `loader/exec/realproc/` host-safe seam; what is new here is
the link shape (crt0 + faced runtime, not plain PE + host cygwin) and the
`AT_BASE = own faced base` publication.

Built and certified as its own step, per DR-0071: the plain-PE stub the WP-41
exec-* certifications drive is untouched -- this is a separate host, not a
replacement -- until the cutover of `accept.sh`'s `build_loader` is certified
against that bar.

## Where the crossing host halts today (measured)

`build-host.sh` links the host in the real-process shape (the exact recipe
`spike/reent-stub-realproc-run` proved) and `drive.sh` runs an ELF image through
it, detached and beside `elfsysv1.dll`. Driving the actual bzip2
(`drive.sh /c/-/el8/accept/bzip2/bzip2-1.0.6/bzip2`, 2026-09-01) advances the
halt off the `accept.sh` run stage's null-`AT_BASE` fault (bzip2 exit 5, no
output, DR-0071's Evidence) onto the concrete next step: with the faced runtime
now hosted as the process's sole runtime, the host reaches its low-window step
and

    elfsysv-stub: the low window at 0x400000 is not held (win_err_refused).

`--self-window` lays the window through `elf_window_reserve`, whose `reserve_at`
is `VirtualAlloc(MEM_RESERVE, PAGE_NOACCESS)` at `0x400000`
(`loader/exec/reserve.c`). In a sole-runtime process that call is refused --
the same refusal `spike/reent-stub-realproc-window-reconcile` measured for the
parent handover, here for the process's own reserve. The measured fix is already
in hand: `spike/reent-realproc-low-window` (verdict=cleared) shows the faced
runtime's own `mmap` places `MAP_FIXED` at `0x400000` for bzip2's span on the
`_dll_crt0` main thread where `VirtualAlloc` is refused.

## Ordered next step

1. Lay the low window through the faced `mmap` `MAP_FIXED` in the real-process
   shape, not `VirtualAlloc` -- reconciled with the DR-0008 segment mapping so
   the window laid and the segments mapped are one coherent faced-`mmap` act
   (`loader/map/elf_map.c` already calls `mmap`, which in this link is the faced
   runtime's). This is `elf_window_reserve`'s `ELFSYSV_REALPROC` branch, gated so
   the plain-PE stub the WP-41 exec-* certifications drive keeps its
   `VirtualAlloc` path unchanged.
2. Map and enter bzip2 inside the process (the existing `elf_map` + `enter.S` +
   `dyn_exec` path, now over the faced `mmap`), with `AT_BASE` carrying the
   faced runtime's own base so the veneer forwarding thunks resolve.
3. Re-run `drive.sh` and confirm the halt advances again; then cut `accept.sh`'s
   `build_loader` onto this host and certify against the WP-41 exec-* bar.
