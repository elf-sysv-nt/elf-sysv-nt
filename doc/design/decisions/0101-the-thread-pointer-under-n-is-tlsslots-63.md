# DR-0101 — under N the thread pointer is `TlsSlots[63]`, reserved through the PEB bitmap

Status: accepted
Date: 2026-09-06
Deciding: the operator, on proposal 0012's open question 1 (decisions of 2026-09-05)
Proposal: 0012
Supersedes: DR-0003, DR-0021
Amends: doc/design/Architecture.md § Thread pointer and TLS

## What was decided

Under substrate N the thread pointer is carrier C1 of spike 6: the word at
`%gs:0x1678`, which is `TlsSlots[63]` in the TEB. The ABI above it is the one
proposal 0011 § 6 wrote: `%gs:TP` holds the TCB pointer, `%gs:TP+8` the
stack-protector canary, `%gs:TP+16` the pointer guard, each one load, with
`TP` = `0x1678`. `tp_set` writes the slot in the target thread's TEB; the
substrate reserves the slot at start by setting bit 63 of the PEB's
`TlsBitmap`, and refuses to start if the bit is already set.

DR-0003 chose carrier C3 (a word below `NtTib.StackBase`) and DR-0021 placed
it at the floor of a Cygwin-managed stack. Both are superseded for this
design. Under H the question does not arise: the guest owns its FS base.

## Why

DR-0003's reason for declining C1 was that a fixed index and `TlsAlloc` draw
from the same 64 bits, so an injected DLL could take the slot; it priced the
slack rather than removing the hazard. `spike/peb-tls-bitmap/` removes it:
`TlsAlloc` consults the PEB's bitmap, the process owns the PEB, and after
`RtlSetBit(TlsBitmap, 63)` seventy allocations hand out every other primary
slot and nine expansion slots and never 63, with a DLL loaded afterwards
changing nothing. DR-0003's reason for preferring C3 was Cygwin's `_my_tls`
precedent and a runtime-owned block; there is no Cygwin runtime under 0011,
and DR-0021's placement is inside a `_cygtls` that does not exist.

What C1 buys over C3 is the ABI as 0011 wrote it. GCC's
`-mstack-protector-guard-reg=gs -mstack-protector-guard-offset=` names one
fixed offset, which C1 has and C3 (two loads through `StackBase`) does not;
under C3 the canary would fall back to the global guard and every package's
codegen default would change. Spike 6 measured C1 at 5.1 cycles a load
against C3's 5.5; the choice is not about speed.

Tier 1 on the ladder after the measurement: with the collision removed, C3's
only remaining argument was a runtime that is absent, and the ABI 0011 wrote
is correct only under C1 or C4. C4 (`ArbitraryUserPointer`) stays declined:
`LdrLoadDll` writes it during every DLL load.

## What it costs, and to reverse

A DLL injected before the substrate's first instruction can allocate slot 63
first; the substrate then refuses to start, loudly, rather than share a word
another module may write. That is a deployment constraint of the same kind as
the endpoint-protection note, and the start-up check is what turns it from a
silent corruption into a red run.

Substrate N's `substrate_thread_pointer` and `carrier_write` changed with this
record and the conformance suite passes 9/9 against it, the `tp_set` group
alone failing when the read is biased. Reversing is a constant in
`substrate_n.c` and in the GCC patch; no on-disk layout carries it.

## Consequences

`Architecture.md` § Thread pointer and TLS names C1, the reserved bit, and
this record beside DR-0024 and DR-0063; DR-0003 and DR-0021 leave its
Settled-by line. `Substrate-Interface.md`'s `tp_set` contract and
`Substrate-N.md`'s table cite this record. The GCC patch for the N target
sets `-mstack-protector-guard-offset=0x1680` (`TP+8`) and the thread-pointer
load to `%gs:0x1678`; `AGENTS.md` § Where autonomy stops closes the TLS
conflict. 0012 open question 1 is settled.
