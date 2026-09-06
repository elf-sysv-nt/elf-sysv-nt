# Reserving a TlsSlots index through the PEB bitmap

Can a process keep `TlsSlots[63]` for itself, so that carrier C1 of spike 6
(one `%gs` load at a fixed index) has no `TlsAlloc` collision to fear from any
DLL loaded later?

Yes. Setting bit 63 of the PEB's `TlsBitmap` with `RtlSetBit`, the export
the loader itself uses, makes seventy following `TlsAlloc` calls hand out
every other primary slot and nine expansion slots and never 63; a DLL loaded
afterwards changes nothing; the slot still reads and writes through
`TlsSetValue`, `TlsGetValue` and a raw `%gs:0x1678` access, and a new thread
starts it at zero, as every carrier did in spike 6. `results-2026-09-06.txt`
is the transcript; `finding=bit-reserved-tlsalloc-never-returns-63`.

## Why it matters

DR-0003 chose carrier C3 over C1 because "a hardcoded index and `TlsAlloc`
draw from the same 64 slots, so a collision ... is a matter of construction",
and priced the slack rather than removing the hazard. Proposal 0011 § 6 then
wrote N's thread-pointer ABI as three single-load `%gs` words, which is C1's
shape and not C3's, and 0012 open question 1 put the conflict to the
operator with this measurement as the recommended way out. The operator
decided for C1 with the bit reserved (DR-0101); this is the fact the record
rests on.

**Gates.** DR-0101; substrate N's `tp_set` and `substrate_thread_pointer`;
the GCC patch's `-mstack-protector-guard-offset`.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Native, `x86_64-w64-mingw32-gcc`, unelevated, under a second.

## Method

The probe finds the PEB through `%gs:0x30` and `TEB+0x60`, the `RTL_BITMAP`
for `TlsBitmap` at `PEB+0x78`, and checks its buffer is the two words at
`PEB+0x80` (q1). It sets bit 63 through `RtlSetBit` (q2), calls `TlsAlloc`
seventy times and records every index returned (q3), writes the slot with
`TlsSetValue` and reads it back through `%gs`, then the reverse (q4), loads
`version.dll` and allocates ten more (q5), and starts a thread that reports
the slot's initial value (q6).

## What this does not reach

A DLL injected before the process's first instruction (an endpoint product's
hook) allocates before the bit is set; the kernel's start-up should check the
bit is clear and refuse loudly if not, and this probe, being a Win32 process
whose CRT already took two slots, cannot measure an ntdll-only process's
starting state. One Windows build; the PEB offsets are the ones every x64
build since Vista has shipped, not a documented contract.
