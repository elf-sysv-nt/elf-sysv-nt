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
   the standalone stub faults before entry -- its low non-PIE window collides
   with `_dll_crt0`'s own mappings -- so item 1 is loader work reconciling that
   contract, not a link flag. See acceptance/reent/stub-realproc.md.

2. The crossing needs a reent-bearing ELF runtime to resolve `libc.so.6`
   against. `accept.sh`'s run stage passes no `--elf-runtime` and bzip2 halts
   needing one; the bare ELF runtime specimens (`libgreet.so`) carry no reent.
   The WP-53 `libc.so.6` veneer -- an ET_DYN forwarding into `elfsysv1.dll` --
   is the runtime this rung and the run stage both wait on, and it is not built
   yet.

3. A reent-consuming ELF specimen entered through the crossing (strtol on an
   overflow, or a string/locale body) whose call returns `LONG_MAX` and sets
   `errno` to `ERANGE` in the reent `__errno` hands back -- the spike's
   `realproc_body_sets_errno_erange`, measured across the loader.

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
