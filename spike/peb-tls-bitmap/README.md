# Reserving a TlsSlots index through the PEB bitmap

Can a process keep `TlsSlots[63]` for itself, so that carrier C1 of spike 6
(one `%gs` load at a fixed index) has no `TlsAlloc` collision to fear from any
DLL loaded later? And where does the array end, so that the canary and the
pointer guard the ABI keeps at fixed offsets from the thread pointer have
somewhere to live?

Yes to the first. Setting bit 63 of the PEB's `TlsBitmap` with `RtlSetBit`,
the export the loader itself uses, makes seventy following `TlsAlloc` calls
hand out every other primary slot and nine expansion slots and never 63; a DLL
loaded afterwards changes nothing; the slot still reads and writes through
`TlsSetValue`, `TlsGetValue` and a raw `%gs:0x1678` access, and a new thread
starts it at zero, as every carrier did in spike 6.

The array ends at the thread pointer. `TlsSlots[0]` is at `0x1480` and
`TlsSlots[63]` at `0x1678`, 512 bytes apart, so `0x1680` is the first byte
past the array: of eighty indices the process holds, written each with its own
magic, none lands on `TP+8` or `TP+16`. The two words the ABI wants therefore
have to be the two slots *below* the thread pointer — the canary at
`%gs:0x1670` (`TlsSlots[62]`) and the pointer guard at `%gs:0x1668`
(`TlsSlots[61]`) — and those reserve exactly as 63 does: with bits 61, 62 and
63 set and every index the earlier questions took released first, seventy
allocations and ten more after a second DLL load hand out none of the three.
All three read and write through one `%gs` load, agree with `TlsGetValue` on
the matching index, and start a new thread at zero.

`results-2026-09-06.txt` is the transcript;
`finding=three-bits-reserved-array-ends-at-the-thread-pointer`.

## Why it matters

DR-0003 chose carrier C3 over C1 because "a hardcoded index and `TlsAlloc`
draw from the same 64 slots, so a collision ... is a matter of construction",
and priced the slack rather than removing the hazard. Proposal 0011 § 6 then
wrote N's thread-pointer ABI as three single-load `%gs` words, which is C1's
shape and not C3's, and 0012 open question 1 put the conflict to the
operator with this measurement as the recommended way out. The operator
decided for C1 with the bit reserved (DR-0101); this is the fact the record
rests on.

DR-0101 then wrote that ABI as `%gs:TP`, `%gs:TP+8` and `%gs:TP+16` with
`TP = 0x1678`, which q7 shows puts two of the three words outside the array
altogether. The glibc port took the two slots below the thread pointer
instead; q7 to q9 are the measurement DR-0106 rests on.

**Gates.** DR-0101, DR-0106; substrate N's `tp_set` and
`substrate_thread_pointer`; the GCC patch's `-mstack-protector-guard-offset`.

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

It then locates the array by writing a magic through `TlsSetValue` and
searching the TEB for it: slot 0 gives the base, slot 63 the last element, the
two together the span, and every index the process holds is written with its
own magic so that one landing on `TP+8` or `TP+16` would be visible (q7). It
frees those indices, sets bits 62 and 61 as well, and repeats the seventy
allocations and the DLL load against all three reservations (q8). Finally it
writes the three words, reads each back with one `%gs` load at `TP`, `TP-8`
and `TP-16`, checks that a raw write to `TP-8` is what `TlsGetValue(62)`
returns, and starts a thread that reports all three initial values (q9).

## What this does not reach

A DLL injected before the process's first instruction (an endpoint product's
hook) allocates before the bit is set; the kernel's start-up should check the
bit is clear and refuse loudly if not, and this probe, being a Win32 process
whose CRT already took two slots, cannot measure an ntdll-only process's
starting state. One Windows build; the PEB offsets are the ones every x64
build since Vista has shipped, not a documented contract.
