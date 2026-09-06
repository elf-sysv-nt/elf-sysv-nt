# A timed futex wait against the NT clock

How late does `RtlWaitOnAddress` wake from a timeout, what does raising the
timer resolution do about it, and does it ever wake early?

At the clock as found (15.6 ms) a 100 µs wait returns about 15.8 ms late and
a 1 ms wait about 14.9 ms late: every short wait lands on the next tick.
After `NtSetTimerResolution` to the finest the kernel offers (0.5 ms), the
100 µs wait is about 0.4 ms late at the median and 1.3 ms at the 99th
percentile, the 1 ms wait under 0.1 ms late at the median, and some waits
return *before* their deadline, which Linux never does. `NtDelayExecution`
behaves the same as the wait, so the clock is the limit and not the wait
object. `results-2026-09-06.txt` is the transcript.

## Why it matters

`FUTEX_WAIT` with a timeout is this call under N (0011 § 6), and the same NT
wait sits beneath H's in-process futex. Linux wakes on an hrtimer a few
microseconds late. A `pthread_cond_timedwait` of 1 ms that sleeps 15 is
visible to anything with a latency budget, and the design's answer (0012
§ 9) was a sentence: raise the resolution at start and record the power
cost. This puts numbers under the sentence and adds a divergence it had
not named: an early return, which the futex path must catch by re-reading
the clock and waiting again, since a caller of `FUTEX_WAIT` is entitled to
`ETIMEDOUT` meaning the deadline passed.

**Gates.** 0012 § 9; phase 4's futex implementation and its timeout loop.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Native, `x86_64-w64-mingw32-gcc`, unelevated, a few seconds. Run on an
idle host: another process holding the resolution high makes q2 read like
q3, and the transcript's q1 line says what the clock was.

## Method

Two hundred waits per case on a word nobody wakes, so the timeout is the
only way out; the lateness is the elapsed time past the deadline measured
with `QueryPerformanceCounter`, and a return before the deadline counts as
early. q2 at the resolution found, q3 after raising it, q4 the
`NtDelayExecution` twin, q5 that the request is released.

## What this does not reach

The power cost of holding the resolution at 0.5 ms, which is the other half
of 0012 § 9's sentence; Windows 10 2004 and later scope a raised resolution
to the requesting process's foreground state in ways this probe does not
exercise. Contention (a waker and a waiter) is not measured; criterion 12 is
where that is. One Windows build.
