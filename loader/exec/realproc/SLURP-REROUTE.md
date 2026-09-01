# Reroute of stub.c's image file read through the realproc seam

WP-56 reent-tls-bringup, item 1, implementing step. `RELINK.md` routed the
stub's option parsing and `--version` output, and `STDERR-REROUTE.md` its stderr
diagnostics; `slurp` -- the `fopen`/`fread` of the ELF image and the
`--elf-runtime` -- was the stub's last direct libc use, deferred there. This
slice moves it behind the same `realproc.h` seam so the source has one image
read in both builds.

## What the seam carries

`slurp` now calls one primitive, `rp_slurp(path, size, &err)`, and turns its
three failure kinds back into the three diagnostics it always emitted -- cannot
read (open), cannot size, cannot load (read). The two `slurp` call sites (the
image and the `--elf-runtime`) are unchanged.

Without `ELFSYSV_REALPROC` the seam is the identity: `rp_slurp` is a `static
inline` in `realproc.h` doing the `fopen`/`fseek`/`ftell`/`fread`/`malloc` the
old `slurp` body did, so the plain-PE build the WP-41 exec-* certifications
drive reads exactly as before. Under `ELFSYSV_REALPROC` `rp_slurp` is
`realproc-file.c`: Win32 `CreateFileA` / `GetFileSizeEx` / `ReadFile` into a
`VirtualAlloc` buffer, calling no libc at all. Reading the image is the stub's
own input work -- the host-safe side of DR-0066's line -- so unlike the output
paths it needs no crossing: the spike (`reent-stub-realproc-window`) found a
plain Microsoft-into-System-V libc call returns without crossing, which is why
`fopen` cannot serve the real-process build and Win32 does.

The window is reserved before `slurp` runs (`stub.c` holds `ELF_WINDOW_BASE`
before it loads anything), so the scratch `VirtualAlloc` cannot land where the
image must go.

## The non-regression proof

The plain-PE build is behaviourally equal: the inline `rp_slurp` runs the same
`fopen`/`fread` sequence, and the full `loader/exec/t/run.sh` -- unit, the
200k-case fuzz, `when`, and every exec-* check including `dyn-cross` and
`dyn-init`, which slurp real ELF images and run them -- passes on the rerouted
tree.

The real-process read is certified natively by a new `file` stage in
`loader/exec/realproc/t/run.sh`: `file-probe.c`, built with
`-DELFSYSV_REALPROC`, writes files on the spot and holds `rp_slurp` to their
bytes -- a body with embedded NULs, an empty file (size 0, non-NULL buffer), and
a missing file (`RP_SLURP_OPEN`). It carries no crossing, so it runs natively,
without the faced runtime, and always runs rather than skipping.

## What stays open

`rp_slurp` opens with `CreateFileA`, which resolves a Windows-form path; the
`file` stage hands it one (`cygpath -w`). But the loader today is invoked with a
Cygwin POSIX path -- `loader/exec/t/run.sh` runs the stub as `-r /bin/echo.exe`
-- which `CreateFileA` does not resolve. In the plain-PE build the inline
`fopen` handles that through the host `cygwin1.dll`; the real-process build
cannot, since its libc calls reach the faced runtime. So the remaining question
for the full real-process relink is how a real-process stub converts the POSIX
path it is handed to a Windows path host-side, without a faced-libc call --
whether the parent that starts the stub passes a Windows path, or a host-safe
conversion is wired. That is a measurement the relink owes, recorded here rather
than guessed; this slice lands the read itself, correct and host-safe over a
Windows-form path, and closes the stub's libc file I/O behind the seam. The
`to-green.tsv` `reent-tls-bringup` signal stays wired to a reent-consuming body
reached across the loader (item 3), not to this read.
