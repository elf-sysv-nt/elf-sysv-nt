# 0001 — WP-32 reopened: mapping over an occupied span is allowed on 3.6.10

Raised 2026-08-30. WP-32 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `loader/map/t/run.sh` fails one case (`case_failures=1`): after reserving
a span, a second placement over that already-occupied span returns `elf_map_ok`
where the test expects a refusal. On 3.0.7 the host refused it.

## Root cause

The mmap-overlap divergence recorded in `spike/map-and-jump/issue/0002`: 3.6.10's
`mmap`/`VirtualAlloc` allows a mapping to land on top of an existing reservation
that 3.0.7 refused. WP-32 relied on the host to refuse the overlap; on the real
environment it must track its own reservations instead, or place differently.
This is a narrower, self-contained divergence than the fault-crossing one — it
does not reach the signal design.

## Status

WP-32 is un-delivered (`doc/status/delivered.txt`) and held
(`doc/status/hold.txt`), so the worker will not attempt it. Its redo waits on the
operator's map-and-jump characterization spike, which pins down 3.6.10's overlap
placement so the redo is grounded rather than guessed. Source retained;
certification withdrawn on the real environment.

## The characterization has run, 2026-08-31

`spike/map-and-jump/characterize-overlap.sh` measured it on both roots; the
reading is in that spike's README and `issue/0002`. The one divergence is that
`MAP_FIXED` over an occupied span is refused on 3.0.7 and allowed on 3.6.10 —
and, worse, the 3.6.10 overlay does not re-zero, so a collision would feed a
second object the first's page bytes without tripping the `.bss`-zero check.
Everything else is identical on both roots.

The grounded redo, unblocked by the measurement but not performed here:

- Replace the reserve's `MAP_FIXED` with a bare `mmap` hint and require the
  returned address to equal the requested span base. On a free span the host
  honors the hint exactly; on an occupied span it relocates, so `got != want`
  is the occupancy signal. Unmap the relocated region and fail
  `elf_map_err_reserve` with a diagnostic that says the span was occupied
  rather than that the reserve failed.
- This reads identically on 3.0.7 and 3.6.10, so the rebuilt package certifies
  on the pinned floor as well as the runtime base. `MAP_FIXED_NOREPLACE` is not
  in 3.6.10's headers, so it is not an option and this stands in for it.
- The occupied-span control in `t/map_test.c` still applies unchanged; it will
  pass against the hint-discriminates reserve where it fails against the
  `MAP_FIXED` one. The comment in `elf_map.c` that claims `MAP_FIXED` "refuses
  rather than displaces an existing mapping on this host" is the false premise
  to remove in the redo.

The redo stays the operator's to unhold; this note records that its
precondition is met, not that it was done.

## Redo authorized, 2026-08-31

The operator authorized the hint-discriminates redo as written above. WP-32
stays in `doc/status/hold.txt` until the rebuilt reserve certifies on the
primary root; the hold lifts on that pass, and this issue closes with it.
