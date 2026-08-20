# Spike 4: the payoff

Unrun. Placeholder; the method belongs here once someone writes it.

**Question.** Does el8's `elfdeps` read a vendor-shaped `Requires` line off a
synthesized `libc.so.6` carrying one verdef node?

**Gates.** Nothing downstream, which is the point. The other three measure
whether the thing can be built; this one measures whether building it repairs
what it was supposed to repair. Run it before anything large is funded.

Per `AGENTS.md`, this directory holds the script that measures it, its sources,
and the transcript the script produced, named by the date of the run.
