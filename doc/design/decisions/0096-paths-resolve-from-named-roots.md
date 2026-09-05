# DR-0096 — paths resolve from named roots

Status: accepted
Date: 2026-09-05
Deciding: the operator, on the publish-gate survey of the split repository
Proposal: none; taken while parameterising the tree for publication.
Amends: doc/design/test-environment.md § Where the environment is named

## What was decided

Three roots are named once, in `bin/roots.sh` and `bin/roots.py`, and every
script and registry in the tree resolves against them instead of writing a
path out longhand.

    ELFSYSVNT_ROOT      the checkout, derived from the sourcing script's own
                        location and never guessed
    ELFSYSVNT_PREFIX    the cross toolchain's install prefix, default
                        /c/-/x-elfsysvnt
    ELFSYSVNT_EL8       the el8 scratch root, default /c/-/el8

A value already in the environment wins over the default. The defaults are
exactly the literals they replaced, which is the property that made the change
safe to take in one pass: every spike regenerates the same transcript on this
host, so nothing had to be recertified to land it.

`test/spike-regen.tsv` and `test/suites.tsv` hold the variable rather than the
value. Their readers — `test/t3-regen.sh` and `bin/check-suites` — expand
before testing a path, by substituting the three names this decision owns and
nothing else. Not `eval`, and not `os.path.expandvars`: a manifest field is
data, and a typo in one should resolve to a path that does not exist rather
than to a truncated path or a command that runs.

`bin/check-roots` is the gate. Transcripts, traces and decision records are
exempt, since a recorded run happened on a real machine at a real path and
rewriting one falsifies it.

## Why

The tree is being published. Ninety-eight files encoded one machine's layout,
and the operator's ruling on the publish-gate survey was that the host name and
the user name are inconsequential while the paths are not — because unlike the
other two, the paths are load-bearing. `spike-regen.tsv` and the `measure-*.sh`
invocations did not merely mention a layout, they depended on it, so the whole
harness was unrunnable anywhere but here. That is the same fact the phase-0
spikes keep having to disclaim: single host, single build, no portability
claim. Parameterising does not make the measurements portable, but it removes
the reason the harness could not even be attempted elsewhere.

Settled by the ladder at tier 2. Correctness does not discriminate — a
hard-coded path is correct on the machine that has it. Reliability does: a
harness that cannot run on a second machine cannot be shown to still work, and
a check nobody else can run is a check that decays without anyone noticing.

## Consequences

A second machine exports the two variables it needs and runs the suite
unmodified. `ELFSYSVNT_ROOT` needs no export at all.

One dependency surfaced while this landed, and is recorded rather than hidden:
`spike/demand-census` reads inputs from the checkout's untracked `a/` annex,
which the split did not carry. Its registry row now resolves to a path in this
repository that does not exist, so `t3-regen.sh` reports it UNMET and the run
INCOMPLETE. That is the correct report. The inputs want relocating or
refetching; certifying around an absent input is the hole the runner exists to
refuse.

A second defect surfaced with it. `t3-regen.sh` chose a spike's transcript with
`ls -t`, so in a fresh clone — where every file carries the checkout's mtime —
it picked whichever tied name sorted first and diffed today's run against a
superseded transcript, reporting a finding as moved. It sorts by name now,
which for a date-named transcript is the same ordering and is a property of the
repository rather than of the filesystem. `spike/ld-tls-relaxation` is where it
was caught: two transcripts, opposite verdicts, and the older one won.
