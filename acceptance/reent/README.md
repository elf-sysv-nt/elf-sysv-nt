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
   reserved for.

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
