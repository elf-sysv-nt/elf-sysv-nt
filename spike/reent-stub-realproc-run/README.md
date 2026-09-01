# reent-stub-realproc-run

WP-56 `reent-tls-bringup`, item 1: the full relink of the loader stub into the
real-process shape, measured on the actual loader source.

Item 1 asks that `loader/exec/stub.c` be relinked in the real-process shape --
`-nostdlib` against the WP-26 `crt0.o` and `-lcygwin`, so `_dll_crt0` brings the
reent up and the faced `elfsysv1.dll` is the process's own runtime -- without
regressing the WP-41 exec-* certifications the plain-PE stub passes. Earlier
spikes carried the pieces in isolation on miniature probes: `reent-stub-link`
(the link), `reent-stub-realproc-window` (the crt0 startup crossing and its
bridge), `reent-stub-realproc-version` (the `--version` path over a
version-probe), `reent-stub-libc-crossing` and `reent-stub-faceload` (the
crossings). The realproc seam (`loader/exec/realproc/`) then landed those fixes
as reusable code the stub includes.

This spike is the composition on the real thing: it links the actual
`loader/exec/stub.c` and its whole translation-unit set, with the seam turned on
(`-DELFSYSV_REALPROC`), in the real-process shape, and measures how far the
linked stub carries when run -- no probe standing in for the stub.

## What it measures

The faced runtime wedges on a host pty, so each run is detached via `cmd` with
stdin from `NUL`, from beside the faced DLL so `elfsysv1.dll` resolves as the
process's own module -- the same shape the sibling reent-stub spikes use.

  - `realproc_stub_links` -- the whole stub set links in the real-process shape.
    `-lgcc` supplies the builtins `-nostdlib` drops; `realproc-cross.c` supplies
    the `memset`/`memcpy` the compiler still emits.

  - `realproc_stub_reaches_version` -- run with `--version`, the linked stub
    emits its `RELEASE` line. That line is a `puts` across the faced runtime
    (`rp_puts`), reached only after the crt0 startup crossing the bridge
    (`realproc-cross.c`'s `cygwin_internal`) carries.

  - `realproc_stub_diag_crosses` -- run standalone with `--self-window`, the
    low-window reserve is refused (no parent held the window) and the stub says
    so. Reaching that message is a path past startup and option handling to the
    window check, and the line crosses fd 2 (`rp_eputs`), so real stub logic
    runs and both crossings -- the startup bridge and the `write(2)` thunk --
    carry, not only the early `--version` exit.

  - `plain_stub_reaches_version` -- control: the same source built plain-PE, the
    seam as identity, reaches `--version` too. The relink is a shape added
    beside the WP-41 one, not a replacement of it.

## What stays with the next step

The `--runtime` face-base half is not measured here and stays with
`spike/reent-stub-faceload`. The stub loads `--runtime` (its `LoadLibraryA` of
the faced DLL) only after the low window is held, and the window is reserved by
the parent front end into the suspended child; a standalone `--self-window`
reserve of the low `0x400000` window is refused in this process. So the real
stub's own faceload is a front-end-driven run -- `reent-face-bringup`'s
`live-run.sh` driving `ELFSYSV_STUB` at the real-process stub -- which is the
next step this rung takes. This spike settles that the stub itself links and
runs in the shape; the live run settles that its `--runtime` reaches the face
base and the veneer thunk returns the reent.
