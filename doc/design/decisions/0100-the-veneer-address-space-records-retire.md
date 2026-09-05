# DR-0100 — the veneer's address-space records retire, and `AT_PAGESZ` is rehomed

Status: accepted
Date: 2026-09-05
Deciding: the operator, through the grant that accepted proposal 0012
Proposal: 0012
Amends: doc/design/Architecture.md § The address space

## What was decided

Four records whose subject is the veneer's address space retire, by the test
DR-0097 applied to eleven others: no governing document in this tree can
state, in the present tense, a thing they settle.

| record | what it settled | why it retires |
|---|---|---|
| DR-0008 | segment mapping goes through the runtime's `mmap`, one region per object, protection at the host granule | there is no runtime `mmap` beneath the kernel; the kernel maps through the substrate |
| DR-0028 | the low window is reserved by the parent into a suspended child | there is no window and no parent handover; the kernel owns the user range |
| DR-0064 | programs get granule, not page, protection precision through the forwarding thunk | there is no thunk; protection is the VMA tree's, page-exact under H and page-rounded under N |
| DR-0077 | the window reconcile is live for the plain-PE shape only | there is no window to reconcile |

DR-0014, that `AT_PAGESZ` reports 4096, the commit granularity, survives: it
is true of 0011 § 4 under both substrates, and it is now homed in
`Architecture.md` § The address space rather than in the retired document.

doc/design/Address-Space.md, which homed all five and carried the veneer's
protocol at length, moves to `doc/history/veneer-address-space.md` with a
heading that says what it was. `bin/check-design-links` drops it from the
governed set, and stops requiring a retired record's `Amends:` line to
resolve, since DR-0077's names the moved file.

## Why

`Architecture.md` sent a reader to `Address-Space.md` for detail, and the
detail was a different system's: Cygwin's `mmap`, a 1 GB window at
`0x400000`, a `MEM_RESERVE` refused from `_dll_crt0`. The review of 0011
found it; 0012 § 10 retires it. The reasoning in the four records was sound
for the arc that took them and is kept at its own paths, as DR-0097 kept the
eleven. Tier 1: correctness discriminates, the subject is absent.

## Consequences

The index Status cells of DR-0008, DR-0028, DR-0064 and DR-0077 gain
`; superseded by 0100`. `Architecture.md` § The address space cites DR-0014
and DR-0098. Nothing here reverses a record; the subjects left.
