# DR-0106 — the canary and the pointer guard take the two slots below the thread pointer; the PEB reservation is three bits

Status: accepted
Date: 2026-09-06
Deciding: the operator, on the handoff of 2026-09-06 § 2 item 4
Proposal: 0013
Amends: doc/design/Architecture.md § Thread pointer and TLS
Partially supersedes: DR-0101, on the placement of the canary and the pointer guard and on the width of the reservation; the rest of DR-0101 stands

## What was decided

Under substrate N the three words of 0011 § 6's thread-pointer ABI are the
last three `TlsSlots` entries, read downwards from the thread pointer rather
than upwards:

| word | `%gs` offset | slot |
|---|---|---|
| TCB pointer (`TP`) | `0x1678` | `TlsSlots[63]` |
| stack-protector canary | `0x1670` | `TlsSlots[62]` |
| pointer guard | `0x1668` | `TlsSlots[61]` |

The substrate reserves bits 61, 62 and 63 of the PEB's `TlsBitmap` at start,
where DR-0101 reserved bit 63 alone, and refuses to start if any of the three
is already set. GCC's `-mstack-protector-guard-offset` for the N target is
`0x1670`, not the `0x1680` DR-0101's Consequences named.

DR-0101 stands in every other respect: carrier C1, the thread pointer at
`%gs:0x1678`, the reservation through the PEB bitmap, `tp_set` writing the
slot in the target thread's TEB, `arch_prctl(ARCH_SET_FS)` returning `EINVAL`.
This record changes where the other two words live and how many bits the
reservation covers.

## Why

DR-0101 wrote the ABI as `%gs:TP`, `%gs:TP+8`, `%gs:TP+16` with `TP` =
`0x1678`, which is 0011 § 6's shape with C1's base substituted. That
substitution does not fit: `TlsSlots[63]` is the *last* element of the array,
so `TP+8` is the first byte past it. Nothing in the record noticed, because
nothing had yet written the two words.

`spike/peb-tls-bitmap/` q7 measures it rather than reading a header. Writing a
distinct magic through `TlsSetValue` and searching the TEB for it puts
`TlsSlots[0]` at `0x1480` and `TlsSlots[63]` at `0x1678`, 512 bytes apart, so
the array's last byte is `0x167f`; of the eighty indices the process holds,
each written with its own magic, none lands on `TP+8` or on `TP+16`. Those two
words are not TLS storage. Whatever the current build keeps there, they are
not the process's to write, and an ABI that puts a canary in one of them is a
corruption waiting for a Windows release that starts using the field.

The two slots below the thread pointer are the only placement that keeps all
three words inside storage the process owns and each of them one `%gs` load,
which is the property 0011 § 6 was written for and the reason DR-0101 chose C1
over C3 in the first place: `-mstack-protector-guard-reg=gs
-mstack-protector-guard-offset=` names one fixed offset, and a canary that has
to be reached any other way falls back to the global guard and changes every
package's codegen default. Reading downwards costs nothing — `TP-8` and
`TP-16` are as immediate as `TP+8` and `TP+16` — and the direction is
invisible above `tls.h`, which is where all three constants are defined.

The reservation extends because the argument for it does. q8 frees every index
q3 and q5 took, sets bits 62 and 61 beside 63, and runs the allocations again:
seventy `TlsAlloc` calls, and ten more after a second DLL load, hand out none
of the three, and all three bits survive. q9 writes the three words, reads
each back with one `%gs` load, confirms a raw write to `TP-8` is what
`TlsGetValue(62)` returns, and finds all three at zero on a new thread —
which is why `clone.S` copies the two guards out of the TCB before the first
protected frame runs, as the glibc port's README describes (on branch
toolchain/glibc-port; it is not in this tree yet). The transcript is
`spike/peb-tls-bitmap/results-2026-09-06.txt`,
`finding=three-bits-reserved-array-ends-at-the-thread-pointer`.

Tier 8 as taken. The glibc port made this choice inside an unattended run and
recorded it only in its own README; three reserved slots where a ratified
record reserves one is a change to the platform ABI, not an implementation
detail, so it was carried to the operator, who accepted it.

## What it costs, and to reverse

Three of sixty-four primary TLS slots are gone from `TlsAlloc` instead of one.
q8's seventy allocations still take fifty-nine primary slots and eleven
expansion slots without a failure, so the cost is two more allocations pushed
into the expansion array, which Windows grows on demand.

The hazard DR-0101 named grows with the count: a DLL injected before the
substrate's first instruction now has three slots it could take rather than
one, and the start-up check refuses on any of them. That is still a loud
refusal rather than a silent corruption.

Reversing is three constants in one header. The glibc port's
`sysdeps/x86_64/nptl/tls.h` defines all three for C and assembler alike,
`substrate/substrate_n.c` has `CARRIER_TEB_OFF` and the bitmap reservation,
and the GCC patch has the guard offset; no on-disk layout carries any of them.

## Consequences

`Architecture.md` § Thread pointer and TLS states the three offsets and the
three-bit reservation and cites this record beside DR-0101.
`Substrate-N.md`'s `tp_set` row and `Substrate-Interface.md`'s `tp_set`
contract cite it. `substrate/substrate_n.c` reserves three bits at start and
refuses on any; the conformance suite's `tp_set` group is unchanged, since the
thread pointer's own offset did not move. `spike/peb-tls-bitmap/` q7 to q9 are
the measurement; `doc/milestones.md` row 48 carries them.

Two consequences land outside this tree, on branch toolchain/glibc-port,
and are named here so that whoever lands it knows what this record already
expects of it: the GCC patch for the N target reads the canary at
`%gs:0x1670`, and the port's `sysdeps/x86_64/nptl/tls.h` is the one place the
three constants are written for C and assembler alike. Neither path exists on
the trunk today, so neither is cited as one.
