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

WP-43 stays held. The hold's reason is upgraded from "measure the cost" to
"the delivery probe kills its own process about two runs in three at
n=20000, mostly on the reserving path; root-cause before certifying." The
run.sh guard and the probe isolation land now regardless, because a
certification suite that can pass on a silent death is a hole whether or not
WP-43 is held.
