# Substitutions ledger

A certification run against a substitute for the thing it certifies is permitted
and recorded here, per the convention in `AGENTS.md`. Each row names what was
substituted for what, where it happened, and what burns the substitution
down — the rerun against the real target that closes the row. A row closes when
that rerun matches, or when the divergence it finds is written down as
justified. An open row is a known gap, not a hidden one.

## Open

| # | Substitute | For | Where | Burns it down |
|---|------------|-----|-------|---------------|
| S1 | WSL glibc 2.43 | el8's glibc 2.28 | The differential certifications of WP-33 (`elf-ldd` / cache), WP-35 (symbol lookup), and WP-40 (auxv / initial process image), which compared this platform's output against a locally available glibc rather than el8's own | WP-T2's pinned el8-shaped environment — a Rocky or Alma 8.10 userland with glibc 2.28, documented in `doc/test-environment.md` — against which those three differentials rerun. The row closes when each rerun matches or its divergence is recorded as justified. |

## Closed

None yet.

## Notes

The substitution in S1 is behavioural, not structural: glibc's observable
resolution, lookup, and auxv semantics are stable across the 2.28-to-2.43 span
for the surfaces these packages certify, which is why the substitute was usable
at all. The burn-down exists because "usable" is a judgement and the rerun is a
measurement, and the project's discipline is to end on the measurement.
