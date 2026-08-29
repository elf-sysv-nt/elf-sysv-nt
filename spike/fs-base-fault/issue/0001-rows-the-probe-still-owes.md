# Issue 0001 — three rows the probe still owes

Status: open
Raised: 2026-08-29, in review of `results-2026-08-29.txt`
Against: `measure-fs-base-fault 1.0`, `fs-fault-probe 1.0`
Owner: whoever next runs this spike, since the probe is already built

The verdict stands. `faults-resumable` is supported by the evidence, and the
row carrying most of the weight is the one with no call site: 1,304,000 reads
under a burner on every processor, every one a fault, every one correct. What
follows does not dispute that.

What it disputes is three claims sitting beside the verdict that were argued
rather than measured, in a run whose probe could have measured all three. Each
is one row. None needs new apparatus.

Ordered by what they put at risk, heaviest first.

## 1. The boundary the verdict rests on

The load-bearing sentence in the transcript is that nothing read through a
zeroed base anywhere in the run. Everything downstream follows from it: a miss
is slow rather than wrong, so the rewriter may be a heuristic, so proposal 0003
has a fallback at all.

It holds because the base is zero, which makes the effective address the
displacement, and every displacement the run exercised was either negative, and
so non-canonical, or a small positive inside the reserved low addresses. That is
structural rather than lucky. It is also bounded, and nobody has found the
bound.

Windows reserves the lowest 64 KB of the address space and will not satisfy an
allocation there, so on this platform the real boundary is `0x10000` rather than
the 0x1000 of a single guard page. Against a `tcbhead_t` of roughly 0x480 that
is a margin of about 139, which is a far more comfortable number than the one
the README currently implies. It is worth having as a measurement because it is
the difference between a claim about TLS layout and a claim about this
operating system.

The row: map a page at a fixed low address, the lowest Windows will grant, then
read `%fs:` at that displacement with the base zeroed. The expected result is
that it returns the mapped value and does not fault, which is the only case in
this whole spike where an access through a zeroed base is quiet. Record the
address at which faulting stops.

Do not skip it on the grounds that the access cannot occur. That it cannot
occur is precisely the claim wanting evidence, and a row that establishes where
the floor is converts an assumption about compilers into a property of the
platform.

## 2. `signal, async`, dropped without a reason

Spike 1 ran twelve cases. Nine carry over here unchanged, `round trip` became
an explicit zero, `thread start` and `fork` were promoted from spike 1's
observations, and `load` reappears as the no-call-site row under the same
burner. That leaves `signal, async` absent, and nothing says why.

It is not an idle omission. Spike 1 exercised it over 3,521,559 checks and lost
the base at check 12,558, so the case is live. More to the point, asynchronous
delivery is the mechanism nearest the coexistence risk the README ranks as
unverified: a vectored handler in a real runtime shares the chain with Cygwin's
own SEH-based signal machinery, and async delivery is where those two meet.
The `hijack` case covers the mechanism at one remove, which narrows the gap
without closing it.

The row: spike 1's own case, carried over the way the other nine were.

## 3. Whether a locked access faults at all

The handler refuses read-modify-write by name, and correctly. Emulating
`addq $1, %fs:-0x28` means emulating `EFLAGS`, and a handler that gets the
carry flag wrong is worse than one that declines.

Locked forms are a harder problem than that, and the README files them under
the same heading. A `lock`-prefixed read-modify-write cannot be emulated in a
fault handler however carefully the flags are done, because the handler cannot
hold the atomicity the prefix promises. So of the two costs proposal 0003 puts
side by side, emulating flags closes the plain arithmetic forms and leaves the
locked ones exactly where they were. The pair is not symmetric and the proposal
now says so.

What is unmeasured is narrower and more useful than that argument: whether a
locked access through a zeroed base faults the same way the unlocked forms do,
with the same code and the same address. If it does, a rewriter can at least
find them and a handler can at least diagnose them. If it reports differently,
the detection story changes too.

The row: `lock addq $1, %fs:-0x28`, and `lock cmpxchg` beside it, with the base
zeroed. Record the exception code and the reported address against the unlocked
form. The handler should refuse both; what is being measured is what Windows
says, not what the handler decides.

## Already corrected, filed here so the record is one place

Three citations were wrong and are fixed in `f71696c`. Spike 1 has twelve cases
of which nine lose the base, not twelve ways to lose it. The ten-of-twelve
count belongs to this spike's event list rather than to spike 1's. And spike 1
recorded three passes, its `round trip` control being the third, so the two
survivors are the two *descheduling* cases that held.

The divergence between the two case lists was also undocumented in both
READMEs, which claimed a straight reuse. `f71696c` says where they part.

## What closes this

A rerun carrying the three rows, with `results-2026-08-29.txt` replaced by a
transcript that names them, and the three starred entries removed from the
README's Not verified section. If a row comes back other than as predicted
above, that is a finding rather than a failure and the verdict paragraph wants
rewriting around it.
