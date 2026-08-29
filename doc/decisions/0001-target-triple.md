# DR-0001 — the target triple is `x86_64-elfsysvnt-linux-gnu`

Status: accepted
Date: 2026-08-29
Deciding: the operator
Proposal: `doc/proposals/0001-triple-and-el8-sources.md`

## What was decided

The target triple is `x86_64-elfsysvnt-linux-gnu`. The project's name goes in
the vendor field; `linux-gnu` stands in the two fields configure actually
reads.

`AGENTS.md` reserves this decision for the operator, and the operator took it
on 2026-08-29, ahead of the count in `spike/triple-fidelity/` rather than by
it.

## Why the vendor field

Of the four fields, `os` and `abi` are what the autotools machinery consults;
`config.sub` passes an unrecognized vendor through untouched, and almost
nothing reads it. So the vendor slot is where a truthful name costs least.
Buildroot has shipped `x86_64-buildroot-linux-gnu` on exactly that reasoning
for over a decade, at the same width, and crosstool-NG ships `unknown` in the
same slot.

The os field was considered and is closed off, for a reason worth stating
because it is not the obvious one. `config.sub` does not refuse `elfsysvnt`
there. It accepts it, by matching `elf*`, the entry in the recognized-os list
that exists for bare-metal targets of the `i386-elf` kind, and `config.gcc`
then reads `x86_64-*-elf*` as bare metal and routes the triple to a target
definition with no operating system beneath it. A refusal would stop a build;
this succeeds and is wrong. Measured against a 2021 `config.sub` on
2026-08-20.

## What it costs to reverse

The triple lands in sysroot paths, in the compiler's installed layout, and in
every build tree that configures against it, so changing it after packages
have been built means rebuilding them. Expensive, not impossible.

The five values of the target definition have to agree — the triple, the
`EI_OSABI` byte, the `.note.ABI-tag` payload, the dynamic linker SONAME, and
what `uname` reports. This record settles the first. WP-10 settles the rest and
now has what it was waiting for.

## When to reopen this

Spike 5 counts the packages that match a literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` and therefore miss silently under any other vendor. Read
its `affected_share` against these bands. They are a judgment rather than a
measurement, and they are written down so that the verdict is read against a
number instead of against whatever the reader's appetite is that week.

| Share affected | Reading |
|---|---|
| under 2% | A patch set. The decision stands and needs no further argument. |
| 2% to 10% | The decision stands, and WP-11's refresh policy carries a named burden with the offending packages listed. |
| over 10% | Reopen. Masquerade as `x86_64-pc-linux-gnu` and move the honest name to `EI_OSABI`, the `.note.ABI-tag`, the loader SONAME, and `uname`. |

At the 2893 source names Rocky 8.10 carries, those bands are about 58 and 289
packages. The lower one sits inside work WP-11 is already committed to, since
every package's vendored `config.sub` needs a refresh policy regardless. The
upper one is a second program of work, which is a different decision and
belongs to whoever is holding the budget then.

Reopening means a new record pointing back at this one. Do not edit this one.

## Where it is written down

`doc/ROADMAP.md`, the assumed-path table. `doc/IMPLEMENTATION-PLAN.md`, WP-10
and WP-11. `doc/elf-technical-breakdown.md`, in `The toolchain and the triple`.
`doc/milestones.md`, spike 5. `AGENTS.md`, under the reserved decisions.
