# WP-T3 — the spike-regeneration runner, and what standing it up turned up

`test/t3-regen.sh` reruns every spike's script and holds the fresh output to its
committed transcript, so a spike whose script has rotted fails the way a broken
unit test fails. The done-when is proven two ways: `test/t3-normalize-test.sh`
shows that numeric drift and a compiler bump read as no change while a changed
verdict word or a truncated transcript fail, and a live injection — breaking a
spike's compiler invocation — makes the runner report the spike rotted and exit
non-zero. This note records what the first full run surfaced, because a rot
detector earns its keep on its first pass or not at all.

## One baseline was genuinely stale, and is refreshed here

`spike/map-and-jump/results-2026-08-31.txt` is added: the spike regenerated on
the primary Cygwin root (3.6.10). Its only prior transcript,
`results-2026-08-29.txt`, was captured on the retired rhel root (3.0.7) — its
`C:\-\rhel\root\...` stub paths give it away — which DR-0038 replaced as the
build and certification environment. On the primary root the spike emits a few
auxv probe lines the older capture lacks and a different live memory map, but
the verdict is unchanged: `verdict=yes`, three map cases and two refuse cases
passing, `huge` failing as the control it is. This is the one transcript the
environment move had left behind; the runner is what noticed.

## Three others were already current, and the runner confirms them

`ld-tls-relaxation`, `versioned-libc` and `triple-fidelity` were re-verified onto
their certified environments in earlier commits — WP-12 gave `ld` its refusal of
fs-presuming TLS relocations, and closing substitution S1 moved the el8
references onto rocky8's glibc 2.28 and the harvested vendor dump. Those
transcripts are canonical in HEAD already, and this run reproduces them:
`verdict=ld refuses the fs-presuming TLS relocations`, `probe_libc=ldd (GNU libc)
2.28`, and `affected_share=1.1%` against `/c/-/el8/dump`. Nothing about them
changes here; the runner simply certifies that they still hold.

## A defect in the runner's own manifest, fixed before it could mislead

`abi-crossing` and `map-and-jump` each hold two characterization scripts, and
each script owns its own transcript — `abi-crossing.sh` beside
`characterize-fault-through.sh`, `map-and-jump.sh` beside
`characterize-overlap.sh`. The first manifest paired a script with "the newest
`results-*.txt` in the directory," which matched a sibling script's transcript
and reported a divergence that was really a mis-pairing. The manifest now names
the transcript each script regenerates and the runner diffs against that.

## What the runner ignores, and why

A rerun reproduces findings, not measurements. The runner compares the spike's
words — case labels, pass/fail, verdicts — and neutralizes everything that moves
between runs without changing the finding: numbers, hex addresses, dates, the
provenance header naming the compiler and kernel, live temp paths (mktemp
suffixes and the root a capture ran under, collapsed to TMPPATH), and the rows
of a VirtualQuery memory survey (MEM_FREE, MEM_COMMIT, MEM_RESERVE), whose
region list varies every run and is context rather than a finding.

## Two properties the run has to have to mean anything

It reads its manifest on a dedicated file descriptor, not stdin, because a
regeneration command that consumes stdin — `wsl.exe` does — would otherwise
swallow the manifest rows that follow it and drop them from the run in silence.
And a spike skipped for a missing input is reported as unchecked and makes the
whole run INCOMPLETE rather than passing quietly: a certification that goes green
while skipping its checks is the same hole WP-43's `run.sh` had. A spike the
manifest marks SKIP by design — no standalone regeneration, like
`cygwin-from-source` and `demand-census` — is not applicable here and does not
taint the verdict.

## Run it on an unloaded host

The host-measurement spikes time events whose outcome bends under scheduler
pressure: `measure-fs-base`'s `apc:pass` reads as `apc:fail`, and `measure-shape`
comes back empty, when heavy builds run alongside. Run alone, both are stable and
reproduce their transcripts exactly. So a FAIL on a measurement spike is
re-confirmed alone before it is believed to be rot, and a clean certification is
taken with nothing else building.
