# reent-stub-faceload

WP-56 road-to-green `reent-tls-bringup`, item 1's face-base half. Measures
whether a real process of the faced runtime reaches `elfsysv1.dll`'s base
without the `error 1114` cygheap wedge, so `--runtime` can publish that base to
the ELF image through `AT_BASE`.

## The question

The `reent-face-bringup` live run halts on `face_base_via_runtime=no`. The
loader's `--runtime` option `LoadLibraryA`s the faced `elfsysv1.dll` from the
plain-PE cygload stub, and that wedges: the faced DLL's cygwin init reserves its
cygheap at a fixed high address and fails when the foreign-PE host never
reserved it -- `heap allocated at wrong address ... error 1114` (DR-0060). So
`AT_BASE` carries no face base and the veneer's thunk null-faults.

DR-0060/0066/0067 name the shape that clears it -- a real process *of* the faced
runtime: linked `-nostdlib` against the WP-26 `crt0.o` and `-lcygwin`, so
`_dll_crt0` brings the reent up and the faced DLL is the process's *own*
runtime, loaded and cygheap-reserved at startup rather than by a later
`LoadLibraryA`. This spike measures the fact the full stub relink turns on: in
that shape, is the faced base reachable, and does the literal `--runtime`
operation return it without 1114?

## What it measures

`faceload-probe.c` is that host -- the `stub-abi-probe` link and startup bridge
(`spike/reent-stub-realproc-window`), reporting only through kernel32 so no
marker is itself a crossing. `measure.sh` builds it twice and runs each detached
via `cmd` from `NUL` (the faced runtime wedges on a host pty), and reports:

  - `realproc_host_links` -- the real-process link succeeds.
  - `startup_gated_without_bridge` -- without the crt0 bridge the host still
    faults before main, the control that the shape, not the probe's own code,
    is what reaches the faced runtime.
  - `startup_reached_main` -- with the bridge, startup reaches main.
  - `own_runtime_base_present` -- `GetModuleHandleA("elfsysv1.dll")` returns a
    base: the faced runtime is already the process's own module.
  - `faceload_via_loadlibrary` -- the literal `--runtime` op, `LoadLibraryA` of
    the faced DLL: `succeeds` (returns a base) or `wedged` (fails, with the
    Win32 error alongside).
  - `faceload_base_matches_own_runtime` -- the two ways `--runtime` can reach
    the base agree.
  - `host_survives_faceload` -- control survived the whole measurement.

## The finding (results-2026-09-01.txt)

The real-process shape clears the wedge. `LoadLibraryA` of the faced DLL
`succeeds` and returns the same base `GetModuleHandleA` already reports -- the
faced runtime is the host's own, so the call bumps a refcount rather than
re-reserving the cygheap, and no 1114 arises. This is the face base `--runtime`
would publish through `AT_BASE`, reached the sanctioned way. The plain-PE
cygload host's 1114 is a property of that shape, not of the load; the shape
DR-0060 chose does not carry it.

The measurement stands to the *host-to-face* half only -- that the base is
reachable without the wedge. Turning it into the live crossing's positive
result (`reent-face-bringup`'s `reent_live_run=pass`) is the full relink of
`loader/exec/stub.c` into this shape, item 1's remaining finishing work; this
spike is the certified foundation that relink stands on, not the relink.

## Reproducing

`bash measure.sh` from this directory; registered in `test/spike-regen.tsv`. It
needs the faced DLL (`a/build/wp27-face/elfsysv1.dll`) and the WP-26 build tree
(`crt0.o`), both gitignored build products; absent either, it SKIPs to
`verdict=yes`. Addresses in the transcript are context and move; the findings --
the `succeeds`/`yes` words and the verdict -- reproduce.
