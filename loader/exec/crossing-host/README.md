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
branch (`munmap` the stray reserve, then `fail(... errno)`), and faulted there:
`fail`'s `vsnprintf` and the `errno` macro (`__errno_location`) were themselves
faced calls the map/enter path did not then cross, so the error path SIGSEGV'd
before it could report -- the obstacle the crossing below removes.

The plain-PE stub does not hit this: `elf_window_reserve` `VirtualAlloc`-reserves
the low window first, so its bare-hint `mmap` lands on the existing reservation
(the design in `doc/decisions/0008-mmap-granule-protection.md`, which
deliberately avoids `MAP_FIXED`). Under `ELFSYSV_REALPROC` the window reserve
became bookkeeping-only (the low window reads free at `_dll_crt0`,
`spike/reent-realproc-low-window`), so a bare hint has no reservation to land
on and the faced arena is free to place elsewhere.

The map/enter error-report path now crosses the boundary, so that placement
refusal reports instead of faulting. `fail`'s `vsnprintf` resolves, under
`-DELFSYSV_REALPROC`, to the host-side `rp_vsnprintf` the realproc seam already
carries (no crossing -- it is this host's own formatter), and the `errno` macro
resolves to `rp_errno()`, a `sysv_abi` thunk to the faced `__errno_location`
(`loader/exec/realproc/realproc-cross.c`); both are gated on the flag the
crossing-host build alone passes, so the plain-PE stub keeps libc's own
`vsnprintf` and `errno` and the WP-41 exec-* bar stays green (10/10). Driving
bzip2 (2026-09-01) advances the halt off the blind `SIGSEGV`: the reserve
refusal is now reported, `drive_rc` 139 -> 1,

    elf_map_err_reserve at mmap: span of 0x50000 bytes at 0x400000
    is occupied; the host relocated the reserve to 0x6ffffffb0000

so placement is now a diagnosed refusal, not a fault. With the report path open,
both placement routes were measured against the actual faced arena, and both
fail at the fixed low window: the bare hint is relocated high to
`0x6ffffffb0000` (above), and a probe forcing `MAP_FIXED` at `0x400000` is
refused outright (`reserve of 0x50000 bytes at 0x400000 refused (errno 0)`,
measured with a temporary `MAP_FIXED` gate since reverted). The low window reads
free (`spike/reent-realproc-low-window`) and the host's own image is based high
at `0x100400000` (so it does not occupy `0x400000`), yet the faced Cygwin arena
neither honours the low hint nor accepts `MAP_FIXED` there. Why the faced arena
refuses a free low fixed placement -- an `mmap_min`-style floor, an arena that
starts high, or a reservation `MAP_FIXED` still needs -- is unmeasured, and it
is the fork the next step must settle before choosing a placement.

## Ordered next step

1. Settle the placement fork with a registered spike, not a guess. The two
   routes are both measured-refused at the fixed low window (above), so the open
   question is empirical: what does the faced Cygwin arena require to place an
   anonymous reserve at a free `0x400000`? Spike each surviving candidate before
   the tier narrows -- (a) a host `VirtualAlloc` reserve of the low window under
   `ELFSYSV_REALPROC` (the plain-PE mechanism of `0008`, reinstated for the
   real process) so the faced bare hint lands on it; (b) reserving the window
   through the faced `mmap` itself so the arena records it; (c) whether the
   faced `mmap` will `MAP_FIXED` onto a region already reserved by (a) or (b)
   even when it refuses a bare fixed request -- per the reproducible-spike
   contract (self-identified, registered in `test/spike-regen.tsv`, dated
   transcript). Let the evidence collapse the fork, then apply the placement in
   `elf_map` gated on `ELFSYSV_REALPROC`; re-run `drive.sh` and confirm the
   placer lands at `0x400000`.
2. With placement landed, control should reach entry with `AT_BASE` carrying
   the faced runtime's own base so the veneer forwarding thunks resolve. Cross
   any remaining faced call the reloc/init/enter path (`elf_reloc`,
   `dyn_init.c`, `enter.S`) makes once placement reaches it. Re-run `drive.sh`
   and read the new halt.
3. Then cut `accept.sh`'s `build_loader` onto this host and certify against the
   WP-41 exec-* bar.
