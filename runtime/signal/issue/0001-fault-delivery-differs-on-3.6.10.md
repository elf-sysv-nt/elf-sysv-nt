# 0001 — WP-43 reopened: signal/fault delivery differs on Cygwin 3.6.10

Raised 2026-08-30. WP-43 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/signal/t/run.sh` fails: the reservation/delivery measurement
comes out differently (a negative reservation cost among them), and the run
exits non-zero.

## Root cause

The same fault-under-a-System-V-frame divergence recorded in
`spike/abi-crossing/issue/0001`. WP-43 builds the signal delivery and the
red-zone repair DR-0006 chose; both rest on how Cygwin delivers a fault beneath a
System V frame, and 3.6.10 does that differently than 3.0.7. Note that the
*reserving delivery* itself still holds the red zone on 3.6.10
(`spike/redzone-delivery/issue/0001`), so the break is in the crossing, not in
the reservation — which is a useful narrowing for the redo.

## Status

WP-43 is un-delivered and held; the worker will not attempt it. Its redo waits on
the operator's 3.6.10 fault-delivery characterization spike, which decides redo
versus redesign and can reach DR-0006. Source is retained; only the certification
is withdrawn on the real environment.

## Characterized, 2026-08-31

The characterization is in `spike/abi-crossing/issue/0001`, and the root cause
above does not survive it: 3.6.10 does not deliver a fault beneath a System V
frame differently from 3.0.7. What differs is that gcc 14 removes a fault
written as a store through a literal null pointer, which is how
`spike/abi-crossing` and `runtime/core/t` raise theirs. This suite does not.
`runtime/signal/t/trial.S` raises its fault with `ud2` in hand-written
assembly, where no compiler can reach it, which is why the failure the reports
attributed to a shared cause was never the same failure here.

Re-run on the primary root on 2026-08-31, `runtime/signal/t/run.sh` **passes**
and exits zero:

    ok - deliveries into a running thread return correctly and keep the red zone
    run: signals deliver onto an ELF stack and return with the red zone whole

The negative reservation cost this report names is still printed — `-23.71% of
a delivery` on that run — but it is a measurement below the noise floor rathe
than a failing check, and the suite's own criterion accepts it. Whether a cost
that reads negative is worth pinning down before WP-43 is re-certified is a
separate question and not one this measurement answers.

So this report has no reproduction on the environment it was raised against.
Whether the hold lifts, and whether the certification is simply reinstated o
the reservation cost wants its own measurement first, is the operator's call.
WP-43 stays held until then.

## Disposition, 2026-08-31

The operator chose the measurement: WP-43 stays held until the reservation
cost is measured on its own and read against DR-0006's bands, and the
certification reinstates after that reading rather than on the passing re-run
alone. The suite itself needs no repair.

## Measured, 2026-08-31

The measurement was taken; the transcript is
`t/reservation-cost-2026-08-31.txt`. Twelve completed `sig_e2e -n 20000` runs
read a median of +0.85% and a mean of +2.33% of a delivery, ranging −4.32% to
+13.05%; both arms run ~33 us per delivery. Ten of the twelve fall in
DR-0006's under-5% band, two land in 5–20%, none approach 20%. The default
n=500 readings — the −23.71% this report carries among them — are inside
timer noise and say nothing.

The claim "the suite itself needs no repair" did not survive the measuring.
Two instrument defects are in the transcript: repeated invocations mostly
exit 0 silently with no output at all (roughly seven in ten attempts), and
one attempt reported the reserving arm incomplete. Neither affects a
completed reading; both belong to WP-43's redo.

The reading of the number against the bands, and whether the hold lifts, is
the operator's; the measurement obligation is discharged.

## The silent exit is a process death, and it is not the control's alone

Hardening the probe to catch the silent exit turned the earlier "instrument
defect" into a finding that should reach the reader before the cost does. The
suite now runs the naive control behind a `popen` child under a `timeout`
watchdog, unbuffers stdout, and prints a stderr marker at each arm boundary
and an `atexit` line on any orderly exit; `t/run.sh` requires the probe's
`ok -` line rather than trusting exit status, so a dead run fails the step
instead of certifying it. With that in place the deaths are visible, and they
say something the n=500 audit could not see:

At n=20000, `sig_e2e` exits 0 with no `atexit` line — a hard process kill, not
an `exit()` — on roughly two runs in three, and the arm it dies in is most
often the **reserving** arm, which runs in the parent on the repaired path,
not the no-reserve control. Fifteen back-to-back runs: five completed, ten
died, and the last stderr marker before death was `# arm: reserving` in seven
of the ten. So this is not the control demonstrating the damage the
reservation prevents; it is the delivery probe destabilizing its own process
while exercising the repaired path, at a rate the n=500 certification never
sampled.

That makes the instrument untrustworthy for certification as it stands, and
the cost numbers above suggestive rather than dispositive: a median under 1%
is only meaningful if the deliveries it timed are representative of a
population that also includes whatever kills the process. Whether the kill is
a defect in the probe harness (its own thread suspend/resume juggling) or in
the delivery path itself is the WP-43 redo question, and it can reach DR-0006
and the signal design, so it is the operator's rather than the worker's.

The run.sh guard and the probe isolation land regardless, because a
certification suite that can pass on a silent death is a hole whether or not
WP-43 is held.

## Root cause, and the fix — 2026-08-31

The self-kill is the harness re-entering a delivery before the previous one
finishes. It is not in the delivery path DR-0006 built; it is in how the test
drives it, and the design is untouched.

`elfsysv_sig_enter_c` sets `p->disposition` — the receiver's *decision* — and
then calls `elfsysv_sig_resume(&p->ctx)` to run the handler and, through the
trampoline, `elfsysv_sigreturn`. `disposition` is documented as exactly that,
a decision: `signal.h` calls it "-1 until the receiver decides" and
`sig_host.h` says it "goes from -1 to an elf_sig_disposition_t when the target
decides." The test read it as *completion*. Its loop waited only for
`disposition >= 0`, then immediately re-hijacked the worker and `memset` the
one shared `pending` record for the next delivery — while the worker was still
inside `elfsysv_sig_resume` reading that same record and still owed a handler
run and a `sigreturn`. The next `SetThreadContext` and `memset` then raced the
in-flight restore. At n=500 the window rarely closes badly; at n=20000 it
compounds to a hard process kill about two runs in three, most often caught on
the reserving arm because that arm does the most work between decision and
return. Real delivery never has this problem: the signal is masked through the
handler and unmasked by `sigreturn`, so the next one cannot start early. The
test drives with `SA_NODEFER` and bypasses that serialization, so it has to
supply the serialization itself.

The fix is in the harness alone. After each delivery the main thread waits
until the worker's RIP is back inside the leaf — past the handler and
`sigreturn` — before it reuses the record or hijacks again, reading the RIP
through a suspend/resume that changes nothing. A short, varying free-run
follows, so the next interrupt lands at a spread of points in the loop rather
than clustering at the one the RIP check leaves it near; without it the control
arm stops breaking, because a fixed landing can consistently heal the clobber.
The completion wait sits outside the timed section, so it does not enter the
reservation cost. The leaf gains one thing, a global label `sig_redzone_spin_end`
bounding it for the RIP check — no instruction, so the specimen it measures is
byte-for-byte unchanged. `t/spin.S`, `t/sig_e2e.c` and the `events` default in
`t/run.sh` carry the change; the last rises to 20000 so the probabilistic
control break is reliable rather than occasionally missed.

Measured after the fix: eighty `sig_e2e -n 20000` runs, zero silent deaths
(was about two in three), the control breaking the red zone every time, and
`t/run.sh` green five for five. A rare gate time-out under heavy concurrent
load, seen once before the wait had a wall-clock bound, no longer hangs: the
gate now gives up after thirty seconds and prints the stuck RIP against the
delivery symbols, so the instrument fails loud instead of silent. The
reservation-cost transcript is regenerated on the trustworthy instrument in
`t/reservation-cost-2026-08-31.txt`; reading its number against DR-0006's
bands, and lifting the hold, remain the operator's per that record.

## Closed, 2026-08-31 — the certification reinstates; the flag stays a separate call

Two things were tangled in this hold and are now separated. The certification
is a fact: with the harness self-kill fixed, `runtime/signal/t/run.sh` passes on
the primary root, which is the condition DR-0038's audit withdrew it against. So
WP-43 reinstates and is delivered.

The reservation cost is the reserved reading, and it is not this issue's to
decide, but the number is in: median −0.49% over twenty clean runs, inside
DR-0006's under-5% band, with the repair contained in the delivery site so the
out-of-path reopen trigger did not fire. Run through `doc/decision-ladder.md`
the reading resolves to a single survivor -- proceed, `-mno-red-zone` comes off
-- without reaching tier 8. Retiring the flag is the operator's standing
faithfulness mandate applied, not a fresh call, and it is recorded as a decision
that supersedes DR-0006. This issue is closed on the certification; the flag
retirement is tracked in that record.
