# WP-T4 in embryo — building a vendor package against this tree

The WP-T4 in the implementation plan is the harness that runs over the whole el8
set and belongs to `rhelcyg-8.10`. It is listed there as the criterion the rest
of the program serves: a tree that passes WP-T1 through WP-T3 and fails this one
has not done the job. This directory is that harness in embryo — the same
pipeline over one named leaf package, which is the overall done-when WP-56
carries: a named small vendor package, built by WP-T4's harness in embryo,
compiles, links, runs its own test suite, and passes it.

It runs today and gives a real per-package verdict. The verdict is not yet green,
and it says exactly why: it names the libc symbols the package needs that the
runtime does not yet resolve. As WP-56 wires those slices, the same command's
verdict improves on its own until the package runs.

## Running it

    export PATH=/c/-/x-elfsysvnt/bin:$PATH
    ./accept.sh bzip2          # one package
    ./accept.sh                # every package in packages.tsv
    ./accept.sh -t bzip2       # the key=value line alone

`accept.sh` fetches the pinned `.src.rpm` from the Rocky mirror (checked by
sha256, reused when the local copy still matches), lifts the source out with the
project's own `rpmx.py`, builds it with `CC` set to the cross compiler, reads the
built ELF's undefined libc symbols, and matches them against
`veneer/classification`. The fetch writes through a redirect rather than `curl
-o`, so a host with only a Windows curl — no Cygwin curl beside it — does not
choke on a POSIX output path.

## The verdict, and what it means

Each symbol the package imports falls into one of the veneer's buckets:

- **forward** (buckets 1 and 2) — resolves to a runtime export, same-name or
  aliased. Works now.
- **wired** (bucket 3, slice crossed) — a translation the wiring layer wrote and
  the live crossing certified stands behind it. Functional now (DR for the
  certified-shim rule).
- **shim** (bucket 3, slice not crossed) — a runtime export exists but the ABI
  differs and no crossed slice covers it yet. WP-56 writes these slice by slice;
  functional once written and crossed.
- **filled** (bucket 4, in the filled manifest) — a synthesized, certified body
  stands behind a name Cygwin does not export (DR-0052). Works now.
- **stub** (bucket 4) — nothing behind it. Fails predictably until something is.

The overall reading is `does-not-build` if the cross build fails, `needs-wiring`
if any unwritten shim or bare stub remains, and `ready` when every symbol
forwards, is a wired shim, or is a filled stub. Only a
`ready` package is worth trying to run. The run stage runs it: it builds the
loader's dynamic-exec front end and PE host stub (WP-41), which stand in for
`ld-linux`, and enters the built image through the crossing. If the image enters
and exits clean the package's own `make test` runs through the same crossing and
a clean suite is the `passing` verdict WP-56's done-when names; until it enters,
the stage names the obstacle it halts at rather than the wait. `accept.sh -R`
skips the stage, and every launch is under a hard timeout so a wedged loader
cannot wedge the harness. The `run:` field on the key=value line records the
outcome — `passed`, `ran`, `halted`, `no-loader`, or `skipped`.

## bzip2

bzip2 is the first pin: pure C, no external library dependency, a hand-written
Makefile, its own `make test`, and a libc surface of forty symbols in the
earliest-demand slices. It is the cleanest leaf — a failure points at the
runtime, not at the package. It cross-builds to a proper el8 ELF — `EXEC`,
System V OS/ABI, `NEEDED libc.so.6`, `INTERP /lib64/ld-linux-x86-64.so.2`.

On 2026-08-31 its verdict was `needs-wiring`. Of its forty libc symbols,
thirty-four forwarded, five wanted shims (`__errno_location`, `__lxstat64`,
`__xstat64`, `open64`, `signal` — the large-file stat, errno and signal family),
and one was a filled stub (`__ctype_b_loc`, glibc's ctype-table accessor, no
matching Cygwin export, a synthesized and certified body behind it — DR-0052).
It linked and loaded but was five shims away from running.
`results-2026-08-31.txt` records that run.

On 2026-09-01 its verdict is `ready`. Nothing about bzip2 or the veneer's export
surface changed; the five shims did. Their slices — string, filesystem, and
signal — were written and live-crossed against a real el8 userland as WP-56
finished wiring, and the harness now credits a shim whose slice has crossed as
`wired`, the way it already credited a filled stub (see the certified-shim
decision). So the surface reads 34 forward, 5 wired, 1 filled, 0 shim, 0 stub:
every symbol has a certified body behind it. `results-2026-09-01.txt` records
this run. What `ready` still withholds is the green — running `make test` under
the loader's dynamic-exec path — which is the loader's surface, not the
harness's.

The pins live in `packages.tsv`, one row per package with its mirror path,
sha256, build command and built binary. bzip2 is enough to stand the harness up;
the next leaves are added a row at a time.

## Running it through the crossing

On 2026-09-01 bzip2 reads `ready` and the run stage launches it, but it halts
before entry: `elf_map_err_granule` — its two `PT_LOAD` segments carry unlike
protection across a shared `0x10000` granule, which the map layer refuses. This
is the first obstacle a real vendor image hits, and it sits earlier on the
ladder than the reent/TLS and syscall bring-up the crossing specimens deferred.
`acceptance/to-green.tsv` names that ladder and `bin/progress.py green` renders
it; the run stage turns each rung from a claim into a run that either clears it
or names where it stops. `t/run-stage.sh` certifies that the stage launches a
ready package and that `passing` is reported only when a suite actually passed.

## Image shape

A clean symbol surface says the runtime can resolve what a package calls. It
does not say the loader will take the image. The classifier that decides which
crossing an image is owed — `exec_kind_of` (WP-56), the gate the dynamic
crossing driver stands behind (DR-0058) — is the one that answers that, so the
harness reads the built ELF with the loader's own path rather than a second
reading of its own. `t/img_shape.c` runs `elf_parse` (WP-31) then
`exec_kind_of` over the binary and prints its kind, its interpreter, and the
sonames it needs; `accept.sh` builds it once with the host compiler over those
two loader packages and reads every built binary through it.

The verdict now clears two gates in order. A package whose symbols do not all
resolve reads `needs-wiring`. One whose symbols resolve but whose image the
classifier does not call dynamic reads `shape-mismatch` — not the shape the
crossing driver runs, and not ready however clean its surface. Only a package
that clears both reads `ready`. bzip2 reads `kind=dynamic`, interp
`/lib64/ld-linux-x86-64.so.2`, needs `libc.so.6` — an interp-bearing `ET_EXEC`,
exactly the shape DR-0058's driver is written to link — so it clears the shape
gate and the `ready` it already earned on its surface now rests on the loader's
reading, not only the harness's. `t/shape.sh` certifies the helper against a
cross-built dynamic, static, and shared specimen whose kinds are known.
