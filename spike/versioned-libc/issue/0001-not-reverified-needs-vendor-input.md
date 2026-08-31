# 0001 — not re-verified on 3.6.10: needs the vendor input

Raised 2026-08-30 during the environment audit. The build and test environment
moved from the rhel root (3.0.7) to the primary Cygwin root (3.6.10), and the
committed `results-2026-08-29.txt` was measured in the old root.

This spike could not be re-run in the primary root: its regenerate script needs
the vendor material (el8's `elfdeps` reading a `Requires` off a synthesized
`libc.so.6`) that is not present on this machine, so the audit reports it as
NEEDS-INPUT rather than as holding or diverged.

Assessment pending the re-run. What this spike measures is `rpm`/`elfdeps`
behaviour over a synthesized library — a static-analysis pipeline rather than a
Cygwin-host runtime behaviour — so it is unlikely to be sensitive to the
3.0.7-vs-3.6.10 move the way the fault-delivery and mmap spikes are. But
"unlikely" is a judgement, and the point of re-verifying is to replace it with a
measurement. Verdict is provisional on the real environment until the vendor
input is available and the script reruns.

## Re-run attempted, 2026-08-31 — the input is here; the vehicle is not

The vendor tree the audit could not find is on the machine at
`/c/-/el8/versioned-libc` (`fixture`, `ref`, `rpm`, `sysroot`), and
`probe-elfdeps.sh -D` against it gets as far as invoking
`sysroot/usr/lib/rpm/elfdeps` — which is el8's own Linux ELF binary, and no
Cygwin root can execute it. The 2026-08-29 run necessarily executed it through
a Linux vehicle. The operator has since authorized a Rocky 8.10 reference
instance; this spike re-verifies through that instance when it stands, and
stays NEEDS-INPUT on the execution vehicle rather than on the vendor material.

## Re-verified, 2026-08-31 — holds, and against the real glibc this time

The Rocky 8.10 reference instance stands (glibc 2.28, verified), and the
probe re-ran through it against the on-machine vendor tree; the transcript is
`results-2026-08-31.txt`. Every measured value is identical to 2026-08-29's.
The one differing line is the probe's own libc — `ldd (GNU libc) 2.28` where
the old transcript says `ldd (Ubuntu GLIBC 2.43)` — which both confirms the
original run's vehicle was the substituted Ubuntu userland and upgrades this
re-verification to el8's actual glibc, the version the substitutions ledger
wants everything measured against. rpm still writes
`libc.so.6(GLIBC_2.2.5)(64bit)` when pointed at the synthesized library.
Closed.
