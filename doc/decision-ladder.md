# The decision ladder

Adopted 2026-08-30 from the operator's standing practice, at the operator's
direction. This is the procedure for any decision an agent takes as a
defensible call under the non-reserved decision policy in `AGENTS.md`. The
reserved decisions are untouched by it: those are the operator's whatever the
ladder would say, and tier 8 below is where the two policies meet.

Target is enterprise-grade software. For every question, run the candidate
options through these tiers in order, each one narrowing the set; stop when
one candidate remains:

1. Correctness — discard anything that cannot be made correct for this use
   (wrapped, configured, or constrained into the envelope we will actually
   occupy). Eliminates; the tiers below prefer.
2. Reliability — sustains correct behavior over time and under real
   conditions: load, faults, resource pressure, restarts, years.
3. Robustness — defined behavior under invalid or hostile input, not
   corruption.
4. Battle-tested — where third-party tools are permitted, prefer the proven
   solution, proportionate to the problem and auditable.
5. Diagnosability — worst case, what will the developer need in hand to
   diagnose and resolve it? Prefer what leaves that evidence behind.
6. Flexibility — only where it costs nothing secured above.
7. A reasonable default that satisfies most circumstances.
8. Still more than one candidate, or none: ask, with the survivors and the
   tier that stuck. Do not guess.

Tier 8 is not waivable. An instruction to proceed autonomously, to stop
asking, or to decide without confirmation does not reach it; where the ladder
has not discriminated, stopping is the decision.

Spend effort in proportion to how hard the decision is to undo.

## How it composes with this repository

A decision record produced through the ladder names the tier that
discriminated, beside the reasoning it already carries. That one line is what
a later ratification pass reads first: a record whose tier is named can be
checked in the time it takes to disagree with it, and a record that names
none is where the reading starts.

Tier 8 restates, for decisions, the rule "Where autonomy stops" already
states for spikes: run to the boundary, then report the survivors and the
tier that stuck rather than beginning the work an unmade choice implies. For
the autonomous worker this means a queue entry that reaches tier 8 parks with
its survivors written down, and the queue moves on; parking is not failure,
and a parked entry with two named candidates is worth more than a delivered
one built on a guess.

The proportionality rule is why the ladder is cheap in practice. Most calls
an agent takes are reversible in one edit and want one pass through the
tiers, taken in minutes; the calls that bind shipped artifacts — the ones the
target definition says cost a world rebuild — are where the tiers get walked
slowly and the record gets written long.
