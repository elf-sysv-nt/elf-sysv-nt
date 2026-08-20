# Spike 1: FS base persistence

Unrun. Placeholder; the method belongs here once someone writes it.

**Question.** Does Windows preserve a user-written FS base across a context
switch?

**Gates.** The TLS layer, and through it the toolchain target definition. Yes
means ELF-standard `%fs`-relative TLS at native cost; no means a TLS model of
our own. Cheapest of the four and decides the most.

Per `AGENTS.md`, this directory holds the script that measures it, its sources,
and the transcript the script produced, named by the date of the run.
