# DR-0070 — the decision ladder measures before it escalates

Status: accepted
Date: 2026-09-01
Deciding: the operator, directing this amendment to their own standing practice
Proposal: none; taken when an autonomous run parked an empirical fork as a
tier-8 operator decision that a spike could have settled.

## What was decided

`doc/decision-ladder.md` is amended so the ladder distinguishes a fork that is a
fact from a fork that is a value. The tiers narrow on evidence, tier 1 above all:
a candidate is discarded as incorrect only when its incorrectness is known. Where
correctness turns on how the host or the toolchain actually behaves and that has
not been measured, the agent runs a spike to produce the verdict the tier reads,
rather than sliding the unmeasured fork down to tier 8. A fork left undecided for
want of a measurement the agent can take is a spike, not a tier-8 question; only
a fork that needs the operator — a value, an authority call, or a reserved
decision — reaches tier 8. Proportionality gates it: spike when the fork is
empirical and the measurement is cheap against how hard the decision is to undo.

## Why

The ladder said, at tier 8, ask when it "has not discriminated" — but it did not
say that a tier can fail to discriminate for two different reasons. One is that
the choice is genuinely the operator's; that is tier 8. The other is that the
evidence a tier needs is simply not yet in hand — and for correctness, which
turns on measured host behavior, that evidence is a spike's to produce. Reading
the second case as the first sends a measurable question to the operator dressed
as a value judgment.

An autonomous run showed exactly this. Facing how the acceptance crossing should
host the faced Cygwin runtime so a package's imports resolve, it tried the
obvious wiring, saw it fail, named the surviving candidates, and parked at tier 8
as an operator decision. But which candidate is correct is a fact a bounded spike
settles — load the faced runtime by each mechanism, measure whether its heap
comes up where expected and its exports resolve. A measurement that eliminates a
candidate collapses the fork; the operator is then owed only whatever genuine
choice remains, with the evidence attached. The ladder now names that step, so an
agent gets the evidence before escalating.

## Consequences

Every agent that takes a decision through the ladder — the autonomous worker and
an interactive session alike — classifies a fork before escalating it, and runs a
spike to discriminate an empirical one within the proportionality bound. The
worker SKILL references this rather than carrying a bespoke rule. A spike run to
decide is registered and kept on the same terms as any other (the reproducible
contract), and the decision it informs cites it, which is the coupling AGENTS.md
already names — a spike is the evidence behind a decision — now made a step of the
procedure rather than an afterthought.

## What it does not decide

Which forks are empirical. That is judgment the agent still exercises: the test is
whether a bounded measurement discriminates the candidates, and where it does not,
tier 8 stands unchanged. Nor does it lower the tier-8 bar: a spike that leaves
more than one correct candidate returns the fork to the operator, exactly as
before, now better evidenced.
