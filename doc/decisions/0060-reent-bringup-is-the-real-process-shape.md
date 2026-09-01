# DR-0060 — reent bring-up is the real-process shape, not a cygload call

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row.

## Context

WP-56's live crossings are freestanding ELF specimens. Each walks the auxv to
the runtime at `AT_BASE` and calls the faced exports it needs, and each stops
where a freestanding harness stops: the NOSIGFE leaves that stand on no reent,
locale, table or kernel cross and are exercised, and the rows whose bodies
consult Cygwin's reentrancy or thread state are left uncalled. `live-string.sh`
names the boundary in its own header — `memcpy` and `strverscmp` are NOSIGFE
yet return garbage there, because their bodies read a reent this harness never
set up. DR-0055 drew the same line for the SIGFE-heavy slices and handed their
bodies to the differential and to "process bring-up." The `reent-tls-bringup`
row is that bring-up named as a capability, and it carried no signal because
what would satisfy it had not been measured.

`spike/reent-bringup/` measures it, contrasting the two host shapes a
faced-runtime call can be made from: the *cygload* shape, a foreign PE that
`LoadLibrary`s `elfsysv1.dll` and reaches its exports, which is what WP-41's
stub is today; and a *real process of the faced runtime*, an exe linked
`-nostdlib` against the WP-26 `crt0.o` and `-lcygwin` so startup runs the
`_dll_crt0` protocol, the shape `runtime/face/t/fault.c` already uses.

## Decision

Reent bring-up is the real-process shape. The spike settles three points, and
they reproduce:

The real-process shape carries a reent-consuming body. `strtol` on an overflow,
called across the System V face, returns `LONG_MAX` and sets `errno` to
`ERANGE` in the very reent `__errno` hands the caller back
(`realproc_body_sets_errno_erange=yes`). That is a libc body reading and
writing the caller's reent at runtime — what the row asks for — and it is the
shape WP-41's loader already gives every ELF frame, so it is the honest target
rather than a workaround.

The cygload bring-up call does not work. Calling the documented foreign-PE
entry `cygwin_dll_init` in the cygload shape does not return
(`cygload_dll_init=hangs`), the wedge `fault.c` records: the vendor leaves the
main thread marked in-cygwin when `dll_crt0_1` returns early for a dynamically
loaded DLL. So the stub cannot be made to bring the reent up by adding a call
to its present cygload shape; the shape itself is what has to change.

The errno slot being reachable in the cygload shape is storage, not bring-up.
Even with no bring-up at all the faced `__errno`/`__getreent` return a non-null,
stable, per-thread, writable word, equal to the reent base
(`cygload_errno_roundtrip=holds`, `cygload_errno_thread_local=holds`), because
Cygwin keys `_my_tls` off the thread's stack, which every host thread has. This
is why a specimen can read a plausible `errno` and still see a body return
garbage: the word is real, but the body that should maintain it was never
brought up. The row is not met by the slot alone.

## Consequences

`reent-tls-bringup` is certified when the crossing gives the ELF program the
real-process-of-the-faced-runtime shape and a reent-consuming body run through
it maintains the caller's reent. The signal to wire for the row when that
lands is a live test asserting the real-process shape's positive result — a
NOSIGFE reent body setting `errno` through the faced `__errno`, the spike's
`realproc_body_sets_errno_erange` measured across the loader rather than in a
hand-built harness. Until the stub adopts that shape the row stays open; this
record leaves its signal unwired rather than matching the storage-only result,
which would flip the row on a fact that is not the capability.

The stub's move from cygload to a real process of the faced runtime is the work
this points at, and it is WP-41/WP-43-shaped: startup through `crt0`/`_dll_crt0`
rather than `LoadLibrary`, with the console isolation the DLL-facing tests
already use (they run detached via `cmd`, the faced runtime's console wedging on
a host pty). The freestanding live crossings keep their value unchanged — they
certify the bind and the NOSIGFE leaves, which is all DR-0055 ever asked of
them — and the reent-consuming bodies are certified here, by the shape the spike
shows carries them.
