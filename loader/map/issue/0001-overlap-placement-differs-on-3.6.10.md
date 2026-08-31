# 0001 — WP-32 reopened: mapping over an occupied span is allowed on 3.6.10

Raised 2026-08-30. WP-32 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0037. Re-run
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
