# 0001 — WP-22 reopened: fault-through fails on Cygwin 3.6.10

Raised 2026-08-30. WP-22 was certified in the rhel root (Cygwin 3.0.7); the
project builds and certifies in the primary root (3.6.10) per DR-0038. Re-run
there, `runtime/core/t/run.sh` fails: the cross probe reports
`sysv_callee_saved_mask=0x0` and the fault-crossing check does not hold.

## Root cause

This is not a WP-22-specific defect. It is one face of the fault-under-a-System-
V-frame divergence recorded in `spike/abi-crossing/issue/0001`: on 3.6.10 a
fault delivered beneath a System V frame does not cross back the way it did on
3.0.7. `spike/abi-crossing`'s verdict itself flips from `yes` to `no` on this
exact case. WP-43 (signals) and WP-61 (core dumps) fail for the same reason.

## Status

WP-22 is un-delivered (removed from `doc/status/delivered.txt`) and held
(`doc/status/hold.txt`), so the autonomous worker will not attempt it. Its redo
waits on the characterization spike for 3.6.10 fault delivery, which the operato
runs in a dedicated session; that spike's answer decides whether WP-22 is a redo
or part of a redesign. The delivered source is not discarded — nothing binary is
committed — only its certification is withdrawn on the real environment.

## Characterized, 2026-08-31

The spike's characterization is in `spike/abi-crossing/issue/0001` and the
transcripts beside it, and it reaches this report too: the root cause above is
not the one that is there.

Both failing cases raise their fault with `*(volatile int *)0 = 1`, in
`ms_faulter` and in `sysv_direct_fault`. That is undefined behaviour, and gcc 14
concludes the path is unreachable and removes the call site that reaches it.
Disassembling the built `core_test.exe` on the primary root: `ms_faulter` and
`sysv_over_ms` are not in the binary at all, and `sysv_direct_fault` survives
only because its address is taken for `elfsysv_core_unwind_present` — there is
no call to it anywhere. So neither case faults, and "the fault never came back
as a signal" is literally accurate and means the opposite of what it was read
to mean. gcc 7.4 kept both calls, which is why the rhel-root certification
passed.

The host is not implicated. Measured on both roots, a fault in Microsoft code
beneath a non-leaf System V frame recovers identically, and a `sysv_abi`
non-leaf still carries no `RUNTIME_FUNCTION` under gcc 14, so this suite's
`unwind-seam` case and DR-0012 both stand — as the suite's own passing rows
already say.

What the two cases need is a fault the compiler cannot reason away;
`runtime/signal/t` raises its with `ud2` in assembly and was never affected.
Making that change is WP-22's redo and is not this measurement's to make. WP-22
stays held until the operator lifts it.
