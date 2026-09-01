# reent-stub-path -- the real-process stub's POSIX->Windows path question

WP-56 reent-tls-bringup, item 1. The image-read reroute
(`loader/exec/realproc/SLURP-REROUTE.md`) closed the stub's file I/O behind the
realproc seam but left one question open: the real-process build reads with
`CreateFileA`, which resolves a Windows-form path, while the loader is invoked
with a Cygwin POSIX path (`loader/exec/t/run.sh` runs `-r /bin/echo.exe`). How
is that path made openable host-side, without a faced-libc call -- does the
parent that starts the stub pass a Windows path, or can a conversion inside the
stub suffice?

## The measurement

`measure.sh` builds `probe-path.c` with the host toolchain (a normal Cygwin
process, the front end's own shape) and runs it on the loader's real input,
`/bin/echo.exe`. It tries both host-side conversions and opens each result with
`CreateFileA`:

- **P, the parent's route.** The host's own `cygwin1.dll` -- `cygwin_conv_path`,
  not a faced-runtime call -- converts the mount path, then the result is
  opened.
- **S, the stub's route.** `GetFullPathNameA`, the only conversion the
  real-process stub can make without crossing into the faced runtime. It
  resolves against the current drive and directory and knows nothing of Cygwin's
  mount table.

Only P's yes/no and S's yes/no are findings; the Windows path strings are
context, host- and root-specific.

## The finding (measure.sh, 2026-09-01, reproduces)

    parent_cygwin_conv_opens=yes  (-> C:\-\cygwin\root\bin\echo.exe)
    stub_getfullpath_opens=no     (-> C:\bin\echo.exe)
    route=parent-passes-windows-path

`cygwin_conv_path` resolves `/bin` through the mount table to the real Cygwin
tree and the file opens; `GetFullPathNameA` reads `/bin/echo.exe` as a
drive-relative path and produces `C:\bin\echo.exe`, which does not exist, so it
does not open. The conversion the stub could make host-safe is the one that
cannot resolve a mount, and the one that resolves the mount is a `cygwin1.dll`
call -- available host-safe only in the parent, whose `cygwin1.dll` is the
host's, not the faced runtime's.

So the answer is **the parent passes the Windows path**: the front end
(`loader/exec/dispatch.c`, a normal Cygwin process) converts the resolved image
path with `cygwin_conv_path` and hands the real-process stub a Windows-form
operand, which `rp_slurp`'s `CreateFileA` opens with no faced-libc call. The
implementing step wires that conversion into the front end's `build_command`
operand for the real-process shape; the plain-PE stub keeps the POSIX path its
inline `fopen` resolves through the host `cygwin1.dll`. It has landed:
`loader/exec/IMAGE-WINPATH-REROUTE.md` records the `exec_image_operand` seam,
the `--real-stub` converter, and the unit test that holds the operand decision.

Native only: no faced runtime, no build products, so it always runs. It SKIPs
(verdict yes) only if `/bin/echo.exe` is absent. Registered in
`test/spike-regen.tsv`.
