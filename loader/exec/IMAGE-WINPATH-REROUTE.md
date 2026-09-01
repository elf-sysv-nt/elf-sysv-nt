# Front-end image-path reroute for the real-process stub

WP-56 reent-tls-bringup, item 1, implementing step. The image-read reroute
(`realproc/SLURP-REROUTE.md`) moved the stub's ELF image read behind the
`realproc.h` seam and left one question open: the real-process build opens with
`CreateFileA`, which resolves a Windows-form path, while the loader is invoked
with a Cygwin POSIX path. `spike/reent-stub-path` measured the answer rather
than guessing it -- `route=parent-passes-windows-path`: the parent's host
`cygwin1.dll` (`cygwin_conv_path`) resolves the mount and the file opens, while
the stub's only host-safe conversion, `GetFullPathNameA`, reads the path as
drive-relative and does not. The conversion the stub could make host-safe
cannot resolve a mount; the one that resolves the mount is a `cygwin1.dll` call,
host-safe only in the parent. So the front end must hand the stub the
Windows-form path.

## What this slice wires

The stub names its image operand and opens it itself, so the operand must be in
the form that stub resolves. Two changes carry that:

  - `dispatch.c` gains `exec_image_operand`, which chooses the operand:
    `cfg->image_path`'s conversion of the resolved path when a converter is
    set and it succeeds, else the resolved Cygwin path unchanged. It returns
    the resolved-path pointer itself when no conversion applies, so the common
    case copies nothing. `build_command` now takes the chosen operand as an
    argument rather than reading `r->file` directly; the program's own vector
    (`r->argv`) is untouched, so what the stub opens and what the program sees
    as its `argv` are named independently.

  - `exec_config` gains `image_path`, a converter callback, null by default.
    The front end (`exec_main.c`) installs `image_to_win` -- a thin
    `cygwin_conv_path` wrapper -- as that callback under `--real-stub` or
    `ELFSYSV_REAL_STUB`. The plain-PE stub leaves it null and keeps the POSIX
    path its inline `fopen` resolves through the host `cygwin1.dll`.

The converter is injected the way the resolver's head-reader already is: the
dispatcher stays free of a direct `cygwin1.dll` dependency, and the front end,
a normal Cygwin process, supplies the one call that needs the host mount table.

## The non-regression proof

The operand decision is a pure choice, tested without a spawn:
`t/unit.c`'s `test_image_operand` holds `exec_image_operand` to five cases
with an injected converter -- no converter yields the path pointer unchanged,
a converter that succeeds yields its output in the caller's buffer, and a
failed conversion, no room, or no buffer each fall back to the path. The full
`loader/exec/t/run.sh` passes on the changed tree: the plain-PE build is the
default path every exec-* check drives, so `--real-stub` off leaves the WP-41
certifications behaving exactly as before.

The real-process open over the converted path is certified separately, by the
`file` stage in `realproc/t/run.sh`, which the SLURP reroute landed. This slice
supplies the operand that stage's `CreateFileA` needs; the `to-green.tsv`
`reent-tls-bringup` signal stays wired to a reent-consuming body reached across
the loader (item 3), not to this path handling.
