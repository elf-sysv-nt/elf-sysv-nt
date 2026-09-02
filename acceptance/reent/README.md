# reent bring-up across the loader crossing (WP-56 road-to-green rung)

The `reent-tls-bringup` rung of `acceptance/to-green.tsv` asks that the runtime's
reent/TLS structure be set up so libc bodies that consult it -- errno, and the
string and locale bodies behind it -- work at runtime rather than merely link.
DR-0060 settled its shape and left its signal deliberately unwired: the row
stays `-` until a reent-consuming body works *across the loader crossing*, not
in a hand-built probe. This file records the foundation that reproduces today
and the ordered path from here to that signal, so the rung is a tracked next
step rather than a claim.

## The foundation, reproduced

`spike/reent-bringup/` measures which host shape carries a reent-consuming body.
Its answer reproduces on this tree (measure.sh, 2026-09-01): the
real-process-of-the-faced-runtime shape -- an exe linked `-nostdlib` against the
WP-26 `crt0.o` and `-lcygwin`, so startup runs the `_dll_crt0` protocol --
returns `realproc_body_sets_errno_erange=yes`: `strtol` on an overflow returns
`LONG_MAX` and sets `errno` to `ERANGE` in the very reent `__errno` hands back.
The cygload shape (a foreign PE that `LoadLibrary`s the faced DLL, which is what
the loader's stub is today) does not: its `cygwin_dll_init` wedges, and its
errno slot is storage without bring-up. The spike is registered in
`test/spike-regen.tsv`, so this foundation is guarded.

## Why the row is still open

The signal DR-0060 asks for is the spike's positive result reached through the
loader, and three things stand between the reproduced probe and that:

1. The loader's PE host stub (`loader/exec/stub.c`, linked by
   `loader/exec/t/run.sh` and `acceptance/accept.sh`'s `build_loader`) is a
   plain PE. It must be relinked in the real-process shape -- `-nostdlib`
   against WP-26 `crt0.o` and `-lcygwin` -- so `_dll_crt0` brings the reent up
   the sanctioned way. DR-0060 calls this WP-41/WP-43-shaped, and it must not
   regress the WP-41 exec-* certifications the plain-PE stub passes today.
   `spike/reent-stub-link/` measures a link change alone: the stub links in the
   real-process shape (once `-lgcc` supplies the builtins `-nostdlib` drops), but
   the standalone stub faults before entry. `spike/reent-stub-realproc-window`
   locates that fault: the stub links at `0x100400000`, not the `0x400000`
   window it reserves, and what faults is the crt0 startup crossing --
   `_cygwin_crt0_common` calls `cygwin_internal` Microsoft-style into the faced
   runtime's System V veneer. Interposing the crossing reaches `main`; a plain
   `printf` into the faced libc then still does not cross. So item 1 is not a
   window reconciliation but crossing the Microsoft <-> System V ABI boundary at
   every host-to-faced-runtime call -- the reframing the decision record and
   `acceptance/reent/stub-realproc.md` carry -- not a link flag. The
   implementing relink has landed: `loader/exec/stub.c` now includes the
   `loader/exec/realproc/` seam and routes its option parsing and `--version`
   output through it, proven not to regress the WP-41 exec-* bar
   (`loader/exec/realproc/RELINK.md`). And the face-base half that shape turns
   on is now measured: `spike/reent-stub-faceload` builds the real-process host
   and finds its `--runtime` `LoadLibraryA` of the faced `elfsysv1.dll` returns
   the runtime's base -- the host's own module, brought up by `crt0` -- with no
   `error 1114` cygheap wedge (measure.sh, 2026-09-01). The stderr diagnostics
   have since been rerouted through the measured fd-2 `write(2)` crossing
   (`loader/exec/realproc/STDERR-REROUTE.md`) and the image read (`slurp`)
   through the measured host-safe Win32 read (`SLURP-REROUTE.md`), closing the
   stub's last direct libc file use. That read opens a Windows-form path, and
   `spike/reent-stub-path` measures how the loader's POSIX input reaches it:
   the parent (a host Cygwin process) converts the mount path with
   `cygwin_conv_path`, the stub's host-safe `GetFullPathName` cannot, so the
   parent passes the Windows path. That conversion has since landed: the front
   end converts the resolved image path for the real-process shape
   (`loader/exec/IMAGE-WINPATH-REROUTE.md`). And the full relink is now measured
   on the actual stub, not a probe: `spike/reent-stub-realproc-run` builds
   `loader/exec/stub.c` and its whole translation-unit set with the seam on
   (`-DELFSYSV_REALPROC`) in the real-process shape and finds it links
   (`realproc_stub_links=yes`), reaches `--version` across the faced runtime
   (`realproc_stub_reaches_version=yes`, `rp_puts` past the crt0 startup
   bridge), and crosses fd 2 for its window diagnostic
   (`realproc_stub_diag_crosses=yes`, `rp_eputs`), while the plain-PE control
   still reaches `--version`. So the stub itself links and runs in the shape;
   what item 1 still owes is the `--runtime` face-load driven through the front
   end, since the stub loads `--runtime` only after the low window the parent
   reserves is held.

   That front-end-driven run is now measured
   (`spike/reent-stub-realproc-faceload`): driven through the front end, the
   plain-PE control stub receives the low-window handover and runs past it,
   but the real-process stub is refused it. Linked against `cygwin1.dll`, the
   suspended child already holds the low region before any user code runs, so
   the DR-0028 `VirtualAllocEx` of the `0x400000` window into it fails
   (`win_err_refused`). So item 1's last step is not the faceload -- its base
   reachability is clear in the sanctioned shape (`spike/reent-stub-faceload`)
   -- but reconciling the low-window handover with a cygwin-linked child, the
   child that holds the low region being the same runtime the window is
   reserved for. `spike/reent-stub-realproc-window-occupant` measures what that
   held region is, replacing the inference: walking the child at
   `CREATE_SUSPENDED`, before any user code, the low window is already a
   private `MEM_RESERVE` region over the ~2 MB at `0x400000`
   (`occupant=private-reserved`, `covers-window-base`), so the parent's
   `VirtualAllocEx` of the whole window is refused with `err=487`
   (`ERROR_INVALID_ADDRESS`), while the plain-PE control's window is free and
   its handover succeeds. The collision is only the low ~2 MB -- from
   `0x600000` the window is free -- so the handover fails because it *starts*
   on the child's own low reservation, not because the window is occupied. The
   reconciliation is identification, not eviction: recognize the child's low
   reservation (adopt it, or reserve the window above it) rather than reserve
   over it. That design step is item 1's last, and its constraint is now
   measured rather than guessed. That design step has now landed (DR-0068): the
   parent's `elf_window_reserve_in` keeps the DR-0028 whole-window call as
   its fast path -- the plain-PE stub takes it unchanged -- and adds a
   fallback, taken only when that call is refused, that walks the child's
   window with `VirtualQueryEx`, plans the free sub-spans with the pure
   `elf_window_plan` (recognizing the child's own `MEM_RESERVE` low region
   and refusing only a committed occupant), and reserves each on its own.
   The planner is certified as a pure decision against the low window's own
   constants in `loader/exec/t/unit.c`. The placement-time half has since
   landed (DR-XXXX): `elf_window_yield` surveys the window and releases each
   constituent reservation before it bares the span, and `elf_window_adopt`
   accepts a window covered by one or more reservations, with the release
   decision `elf_window_release_plan` certified as a pure decision beside
   `elf_window_plan` and the whole WP-41 exec bar (unit, fuzz, when, the
   exec-* routes, exec-kind, dyn-cross, dyn-init) unregressed. What item 1
   still owes is the live measurement: driving reserve, adopt, yield and
   place through a real cygwin-linked child rather than the in-process unit
   fixtures.

   The first of those verbs, `reserve`, is now driven live
   (`spike/reent-stub-realproc-window-reconcile`), and it re-aims the design:
   `elf_window_reserve_in`'s reconcile fallback is refused against a real
   cygwin-linked child (`reserve_in=win_err_refused`, `window_covered=no`),
   because the child's low window is not the bare `MEM_RESERVE` the planner
   models but `low_window_occupant=reserved+committed` -- a private reservation
   plus committed pages below the free tail, the runtime's own, present before
   any user code. `elf_window_plan` refuses any committed occupant by design, so
   the fallback returns `win_err_refused`. So item 1's reserve verb does not yet
   clear a real child: the unit fixtures modelled the low region as one
   `MEM_RESERVE`, and the live child carries a committed occupant beside it. How
   the loader should treat a committed low occupant that is the child's own
   runtime allocation -- adopt it, reserve only up to it, reserve around it, or
   otherwise -- is a design step with more than one live candidate, parked for
   the operator (`a/build-blockers.log`) rather than guessed. The spike fixes
   the constraint that step must satisfy and guards that the obstacle
   reproduces.

2. The crossing needs a reent-bearing ELF runtime to resolve `libc.so.6`
   against. `accept.sh`'s run stage passes no `--elf-runtime` and bzip2 halts
   needing one; the bare ELF runtime specimens (`libgreet.so`) carry no reent.
   The WP-53 `libc.so.6` veneer -- an ET_DYN forwarding into `elfsysv1.dll` --
   is the runtime this rung and the run stage both wait on. `spike/reent-veneer-runtime/`
   measures what it provides today (measure.sh, 2026-09-01): `veneer/libc/build-libc`
   builds `libc.so.6` and it carries the whole reent surface -- the
   `errno@@GLIBC_PRIVATE` TLS carrier and `strtol` at its el8 node -- but every
   FUNC/IFUNC body is a single-byte `ret` (`reent_body_is_stub=yes`): the
   `elfsysv1.dll` export each entry reaches lives in `libc-forward.tsv` as data,
   not as emitted forwarding code. So it resolves the crossing at link time but
   consults no reent at run time; item 2 is generating the forwarding bodies
   that reach `elfsysv1.dll` -- where the WP-27 face brings the reent up -- not
   merely building the veneer. The codegen for those bodies has landed
   (recorded in `doc/decisions/` as the runtime-resolving veneer thunk):
   `veneer/libc/generate.py` now emits each `forward-same`/`forward-alias`
   FUNC as a runtime-resolving thunk -- the shape `spike/reent-veneer-thunk`
   pinned -- and `veneer/libc/t/run-tests.sh` certifies the four link-time facts
   on the built `libc.so.6`. What item 2 still owes is the run: the thunks name
   their exports and hold no ELF self-import, but reaching the face and
   returning the reent-consuming result is item 3, behind the built face DLL.

3. A reent-consuming ELF specimen entered through the crossing (strtol on an
   overflow, or a string/locale body) whose call returns `LONG_MAX` and sets
   `errno` to `ERANGE` in the reent `__errno` hands back -- the spike's
   `realproc_body_sets_errno_erange`, measured across the loader.

   `spike/reent-face-bringup` (item 3's spike) now writes and measures that
   run (`live-run.sh`, 0.3). It builds the loader, the veneer, and a
   reent-consuming forward specimen and takes it through the crossing three
   ways. Two of the three now pass: the veneer maps as an `--elf-runtime`
   (`veneer_maps_as_elf_runtime=yes`, once its link was made granule-separable),
   and the specimen enters through `enter.S` with its `strtol` thunk running
   (`crossing_enters=yes`) -- so the ELF crossing is no longer the obstacle.
   What remains is the face-base half: `--runtime`'s `LoadLibraryA` of the faced
   `elfsysv1.dll`, so its base reaches the veneer's resolver through `AT_BASE`,
   is the cygload shape that wedges (`error 1114`, heap-at-wrong-address), so
   `AT_BASE` carries no face base and the thunk null-faults. That is item 1's
   real-process face-load, not the crossing, and its base-reachability half is
   now measured cleared in the sanctioned shape (`spike/reent-stub-faceload`): a
   real process of the faced runtime loads it without the `1114` wedge, leaving
   the full relink of the loader stub into that shape as the remaining step. The
   spike's `verdict` stays `staged` and it stays out of `test/spike-regen.tsv` until that half lands and
   the thunk resolves and returns the reent -- the live run that wires the
   signal below.

Only then is the `to-green.tsv` `reent-tls-bringup` signal wired, to that live
test's positive result, replacing the `-`.

## The subtlety to verify, not assume

`enter.S` already parks the host stack and restores it around the crossing,
because Cygwin finds its per-thread state (`_my_tls`) from the stack pointer and
the ELF world runs on its own stack in the low window. The across-the-loader
reent test is the first time that mechanism is exercised for a body that reads
and writes the reent, so it is where the ELF-frame reent shape is measured
rather than asserted -- the "runtime face at a DLL's width" line the plan's
`Not verified` section keeps open.

## Resolution of item 1's hosting question (DR-XXXX)

The faced-runtime hosting item 1 halts on is resolved, not open: the acceptance
crossing hosts the faced runtime as its own process -- a real process of
`elfsysv1.dll` brought up through the WP-26 `crt0` `_dll_crt0` protocol, so the
faced runtime is the process's sole Cygwin runtime, its own `mmap` performs the
DR-0008 mapping, and bzip2 runs inside it. This is the same real-process shape
DR-0060 named for reent; hosting the runtime and bringing reent up are one act.
It supersedes, for this shape, the host-Cygwin stub plus separate faceload
DR-0060/DR-0066/DR-0067 left in place, and sets aside the parent window handover
(DR-0028) and its cygwin-child reconcile (DR-0068/DR-0069): a process that is the
faced runtime lays its own address space at startup rather than receiving a low
window from a parent. The reserve and adopt verbs are refused against a real
cygwin-linked child (`spike/reent-stub-realproc-window-reconcile`,
`low_window_occupant=reserved+committed`) because they belong to the parent-
handover path this shape does not take. The finishing work is to move
`accept.sh`'s `build_loader` crossing onto that host and run bzip2 inside it,
built and certified as its own step against the WP-41 exec-* bar. That its own
`mmap` maps the fixed low window where the parent handover was refused is already
measured: `spike/reent-realproc-low-window` (milestones row 30) finds a real
process of the faced runtime places `MAP_FIXED` at `0x400000` for bzip2's span
(`realproc_mmap_fixed_window=ok`), the low window reading `free` at its own
`_dll_crt0` startup rather than the `reserved+committed` a suspended foreign
child holds. So the finishing work rests on a measured premise; what remains is
the `build_loader` reshape itself, not a further measurement.
