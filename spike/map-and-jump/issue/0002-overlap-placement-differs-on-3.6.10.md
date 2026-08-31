# 0002 — a placement over an occupied span is allowed on Cygwin 3.6.10

Raised 2026-08-30, re-running `map-and-jump.sh` in the primary Cygwin root
(3.6.10, gcc 14) against the committed `results-2026-08-29.txt`, measured in the
rhel root (3.0.7). The project builds and runs in the primary root.

## What changed

The spike runs six cases. Five still behave as recorded; one fails. In the
`occupied` family, the stub reserves a span and then asks the host to place a
second mapping over that same, already-reserved span. On 3.0.7 the second
placement was refused, which is what the loader relies on. On 3.6.10 the second
placement returns `elf_map_ok` — the host allowed the overlap:

    3.0.7 (committed)   second placement over occupied span: refused  (pass)
    3.6.10 (re-run)     second placement over occupied span: allowed  (case_failures=1)

The core capability the spike exists to prove — a PE stub maps a static ELF,
reserves the low window, and jumps into it, getting control back — still holds;
the overall run even prints `verdict=yes` for that part. What differs is the
narrower host semantic that a mapping cannot land on top of an existing
reservation.

## Why it matters

WP-32 (segment mapping) leans on the refusal: `loader/map`'s certification fails
in the primary root on exactly this case (`second placement over occupied span
is refused FAILED`). So WP-32 is not certified on the environment the project
runs in, and its overlap handling has to account for 3.6.10 allowing what 3.0.7
forbade — either by tracking its own reservations rather than trusting the host
to refuse, or by a different placement strategy.

## What is owed

WP-32 reopens against 3.6.10. Before its redo, the placement behaviour of
3.6.10's `mmap`/`VirtualAlloc` over an occupied span wants pinning down as a
measurement, so the redo is grounded rather than guessed. Until then the
committed transcript's overlap result is superseded on the real environment.

## Characterized, 2026-08-31

The measurement was taken. `characterize-overlap.sh` builds `overlap-probe.c`
and runs it in whichever root invokes it; the two transcripts are
`results-overlap-3.6.10-2026-08-31.txt` (authoritative) and
`results-overlap-3.0.7-2026-08-31.txt` (control). The spike README's "The
overlap characterization" section carries the full reading. In short:

- The divergence is one fact. `MAP_FIXED` over an occupied span is refused on
  3.0.7 and allowed on 3.6.10; all five other measured behaviours are identical
  on both roots. It is a conformance change in Cygwin's `mmap`, not in the host
  — the Win32 layer (`VirtualAlloc(MEM_RESERVE)`) still refuses on both.
- The overlay does not even re-zero. On 3.6.10 the second `MAP_FIXED` returns
  the same address with the prior page contents intact, so a collision would
  hand a second object a page carrying the first's bytes, which the `.bss`-zero
  assertion would not catch.
- There is a bookkeeping-free redo that certifies on both roots. A bare `mmap`
  (no `MAP_FIXED`) is honored exactly on a free hint and relocated off an
  occupied one, so the reserve can drop `MAP_FIXED`, pass the span base as a
  hint, and require `got == want`; occupancy shows up as a relocation. This
  reads identically on 3.0.7 and 3.6.10.
- `MAP_FIXED_NOREPLACE` is absent from 3.6.10's headers, so the clean flag is
  not an option and the hint-discriminates path stands in for it.

The measurement is done; the redo is not, and is not this spike's to do. WP-32
stays held under `loader/map/issue/0001` until the operator lifts it. This
issue is answered on its measurement obligation and can close when that redo
lands.
