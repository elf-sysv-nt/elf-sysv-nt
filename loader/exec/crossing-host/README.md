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
it, detached and beside `elfsysv1.dll`. The low-window step is now cleared:
`elf_window_reserve` gained an `ELFSYSV_REALPROC` branch (`loader/exec/reserve.c`)
that claims the window as bookkeeping without a host reservation, and
`elf_window_yield`'s `ELFSYSV_REALPROC` branch hands the bare, already-free low
window straight to the placer with no MEM_RELEASE dance, since a real process of
the faced runtime laid its own space and the window reads free at `_dll_crt0`
startup (`spike/reent-realproc-low-window`, verdict=cleared). Both branches are
gated on the `-DELFSYSV_REALPROC` the crossing-host build alone passes, so the
plain-PE stub the WP-41 exec-* certifications drive keeps its `VirtualAlloc`
reserve and its parent-handover yield unchanged; the exec-* bar
(`loader/exec/t/run.sh`) stays green (10/10) with the branch in place.

The map/enter path's memory primitives now cross the boundary the sanctioned
way. `elf_map`'s `mmap`/`mprotect`/`munmap` route, under `-DELFSYSV_REALPROC`,
through `sysv_abi` thunks (`rp_mmap`/`rp_mprotect`/`rp_munmap` in
`loader/exec/realproc/realproc-cross.c`, resolved from `elfsysv1.dll`'s export
directory) instead of as direct Microsoft-ABI calls; the plain-PE build keeps
the native calls (the seam macros are inert there) and the WP-41 exec-* bar
stays green (10/10). Driving the actual bzip2
(`drive.sh /c/-/el8/accept/bzip2/bzip2-1.0.6/bzip2`, 2026-09-02) advances the
halt off the ABI-crossing fault: the host now reaches, in order,

    window 0x400000 for 0x3fc00000 held
    runtime elfsysv1.dll at 0x7ffd...
    parsed: 4 PT_LOAD, entry 0x410cee
    exec kind: dynamic
    place -> elf_map -> rp_mmap(0x400000, ...) -> returns 0x6ffffffb0000

The faced `mmap` call crosses and returns (a temporary trace measured the
return value directly, then was removed), so the DR-0066 boundary is no longer
the blocker. The new, measured obstacle is placement. bzip2 is `ET_EXEC`
(`readelf -h`: Type EXEC, entry `0x410cee`), so it must load at its fixed
`0x400000` with load bias 0 -- it cannot be relocated. But the faced Cygwin
`mmap` treats the address argument as a bare hint, not `MAP_FIXED`, and its own
arena places the anonymous reserve high at `0x6ffffffb0000` rather than at the
free low window. `elf_map` reads `got != res_base`, takes its relocation-refuse
branch (`munmap` the stray reserve, then `fail(... errno)`), and faults there:
`fail`'s `vsnprintf` and the `errno` macro (`__errno_location`) are themselves
faced calls the map/enter path does not yet cross, so the error path SIGSEGVs
before it can report.

The plain-PE stub does not hit this: `elf_window_reserve` `VirtualAlloc`-reserves
the low window first, so its bare-hint `mmap` lands on the existing reservation
(the design in `doc/decisions/0008-mmap-granule-protection.md`, which
deliberately avoids `MAP_FIXED`). Under `ELFSYSV_REALPROC` the window reserve
became bookkeeping-only (the low window reads free at `_dll_crt0`,
`spike/reent-realproc-low-window`), so a bare hint has no reservation to land
on and the faced arena is free to place elsewhere.

## Ordered next step

1. Place the fixed-address image into the verified-free low window. The window
   is discriminated free by the reserve step (and the spike), so under
   `ELFSYSV_REALPROC` `elf_map`'s placing `mmap` can pass `MAP_FIXED` to force
   `0x400000` -- occupancy was already checked, which is exactly the condition
   `0008-mmap-granule-protection.md` requires before `MAP_FIXED` is safe. Gate
   it on `ELFSYSV_REALPROC` so the plain build keeps its bare-hint placement.
   Weigh this against reserving the window through the faced `mmap` itself
   (so the arena records it and the later bare hint lands); measure both before
   choosing, per the decision ladder. Re-run `drive.sh` and confirm the placer
   lands at `0x400000`.
2. Cross the map/enter path's remaining faced surface so a map failure reports
   instead of faulting: `fail`'s `vsnprintf` and `errno` (`__errno_location`),
   and any faced call the reloc/init/enter path (`elf_reloc`, `dyn_init.c`,
   `enter.S`) makes once placement reaches it. Re-run and read the new halt.
3. With placement and reporting crossing, control should reach entry with
   `AT_BASE` carrying the faced runtime's own base so the veneer forwarding
   thunks resolve. Re-run `drive.sh` and read the new halt.
4. Then cut `accept.sh`'s `build_loader` onto this host and certify against the
   WP-41 exec-* bar.
