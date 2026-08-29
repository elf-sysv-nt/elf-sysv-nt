# DR-0005 — the `linux` field is a bounded claim, not a lie

Status: accepted
Date: 2026-08-29
Deciding: the operator
Proposal: `doc/proposals/0004-the-bounded-linux-claim.md`

## What was decided

The `linux` in `x86_64-elfsysvnt-linux-gnu` is the kernel field, `gnu` is the
libc field, and both are claims this project means. `gnu` is true without
qualification. `linux` is true along every axis but one: this project satisfies
the Linux kernel ABI by rebuilding against `elfsysv1.dll` rather than by
dispatching system calls, so an object reaching the kernel through a raw
`syscall` instruction is outside the contract the triple advertises.

That bound is the settlement. `doc/target-definition.md` states it, and the
documents that previously called the field a deliberate lie now say what it
claims instead.

DR-0001 is not reopened. The triple is unchanged, and so are the other four
values of the target definition.

## Why a record rather than an edit

DR-0001's own reopen table offers exactly one alternative, the masquerade, and
prices it against a share of affected packages. It has no row for "the value
stands and the reasoning around it was imprecise", which is what happened here.
Editing DR-0001 to fix the field names would rewrite reasoning that was live in
August into reasoning that is live now, and the index forbids that for good
cause. So this record carries the correction and points back.

## What it rests on

Measured on 2026-08-29 against `toolchain/config/`'s pin, upstream timestamp
2026-05-17. `config.sub` names its own variables `kernel` and `os` and
validates the pair under `case $kernel-$os-$obj`; there is no `abi`.
`x86_64-elfsysvnt-linux-gnu` is echoed unchanged.
`x86_64-elfsysvnt-linux-elfsysvnt` is refused, because the script allowlists
ten libc values against the `linux` kernel. `x86_64-pc-elfsysvnt` is still
accepted rather than refused, which reconfirms DR-0001's trapdoor against a
current file rather than the 2021 one it measured.

The bound itself is not a measurement. It follows from the design in
`doc/elf-technical-breakdown.md`, whose second bridge leaves no `syscall`
instruction to catch, and it has not been tested against a package that
insists on inline syscalls, because no package has been built yet.

## When to reopen this

Not on a share of affected packages, which is how DR-0001 reopens and is the
wrong instrument here. Reopen when a package that matters cannot be built
without inline system calls, or when a vendor binary's raw syscalls prove more
expensive to handle than its TLS accesses were. One package is enough if it is
the wrong package, and a hundred harmless ones are not. Either finding widens
the bound rather than changing the triple, and the change lands in the loader,
which is where `doc/proposals/0003-vendor-binary-tls-rewriting.md` already
lives.

A finding that `gnu` is the wrong libc field, which would mean this project had
stopped shipping glibc, is a different decision and a much larger one. Nothing
suggests it.

## Where it is written down

`doc/target-definition.md`, under the limit of the `linux` claim, and in the
`uname` section that used to call the field a lie. `doc/milestones.md`, spike
5. `doc/elf-technical-breakdown.md`, in `The toolchain and the triple`.
`doc/ROADMAP.md`, the assumed-path table. `doc/IMPLEMENTATION-PLAN.md`, WP-10.
`AGENTS.md`, under the reserved decisions.
