
The native substrate: the first real implementation of the nine-call interface
in `doc/design/Substrate-Interface.md`, run under a `/make-it-so` grant on
2026-09-05. Substrate N runs user code as ordinary NT threads in the host
process, reached through a gate; the hypervisor substrate H is a later peer.
This document records what N realises each call with, the construction plan,
and the decision log for the run.

The interface contract is fixed (Substrate-Interface.md) and the certification
bar is written (the conformance suite in `substrate/`). This is therefore
leaf-first construction: N is the leaf the core will sit on, built to a bar it
did not get to relax.

## What N realises each call with

Every mechanism is backed by a landed phase-0 spike, so the risk each carries
was retired before this construction began.

| call | N mechanism | spike |
|---|---|---|
| `as_map` / `as_unmap` / `as_protect` | placeholder-reserved arena, section views, `VirtualProtect`, lazy commit through a vectored handler | 37 (`arena`) |
| `as_clone` | `RtlCloneUserProcess` executive clone | 35 (`nt-clone-fork`) |
| `thread_start` | an NT thread entered at the register state through the gate | 36 (`native-host`) |
| `thread_interrupt` | `SuspendThread` + context rewrite, deferring while in `ntdll`, latching across the gate window | 38 (`hijack`) |
| `thread_context` / `set_context` | `GetThreadContext` / `SetThreadContext`, honouring the 128-byte red zone | 38 |
| `tp_set` / `substrate_thread_pointer` | a word reached through `%gs`, because the FS base does not survive a deschedule: carrier C3 at certification, `TlsSlots[63]` reserved through the PEB bitmap since DR-0101 (2026-09-06), re-certified 9/9; the canary and the pointer guard take `TlsSlots[62]` and `[61]` and the reservation is three bits (DR-0106) | 1 (`fs-base-persistence`), 6 (`gs-thread-pointer`), peb-tls-bitmap |
| `user_copy_in` / `user_copy_out` | fault-guarded copy returning the fault address, never crashing on a bad user pointer | — (in-process for N) |

## Construction plan (leaf-to-trunk, riskiest first)

One work package: N is a single `substrate_n.c` providing `substrate_create()`
plus `substrate_thread_pointer` and `substrate_gate_enter/exit`, linked into the
existing `conformance.c` in place of the mock. The nine calls share one
`struct substrate` state and cannot be split across worktrees; there is no
fan-out here.

The bar is the conformance suite, unchanged where it can be. Order within the
package puts the least-understood integration first: the gate flag interacting
with `thread_interrupt`'s latching and the `%gs` carrier on real NT threads,
then the address-space calls, then `as_clone`, whose realisation crosses a
process boundary and is the one place the in-process harness needs extending.

## Decision log

- **D1 — one work package, no fan-out.** Tier 7. The nine calls share
  `struct substrate` state and link into one `substrate_create`; make-it-so's
  own limit 2 (no two agents edit one worktree) makes the substrate a single
  cohesive leaf, not independent parallel elements.

- **D2 — `as_clone` conformance is driven cross-process, not shadowed.** Tier 1.
  The mock satisfied the `as_clone` contract with a same-process shadow snapshot
  because it had to fit an in-process harness. N's real clone is
  `RtlCloneUserProcess`, which produces a child *process*; certifying it with a
  same-process shadow would test the mock's mechanism, not N's. So the
  `as_clone` group is extended: the parent clones, the child self-certifies the
  copy-on-write contract (reads the parent's pre-clone bytes, writes, confirms
  the write is private) and reports through its exit status or a shared section.
  If that cross-process wiring does not close within the gate's three attempts,
  `as_clone`'s real-clone conformance is the named boundary reported to the
  operator, and the eight in-process calls are the increment that lands — a
  substrate whose clone is proven at the primitive (spike 35) but not yet
  through the harness. It is not faked.

- **D3 — `tp_set` uses the `%gs` carrier, not a bare per-tid word.** Tier 1. The
  mock used a per-tid word to satisfy the round-trip; N must use the carrier
  DR-0003 chose, reached through `%gs`, because that is the mechanism the
  toolchain is built against and the one spike 1 proved necessary (the FS base
  does not survive a deschedule). A test that passed on a bare word would be
  measuring mechanism, not the contract.

- **D4 — the cross-process `as_clone` harness hooks live on `struct substrate`.**
  Tier 7. Driving a cross-process clone through an in-process suite needed a
  capability flag (`clone_cross_process`) and two hooks (`clone_child_certify`,
  `clone_wait`) the `as_clone` group calls; they were added to `substrate.h`
  beside the nine calls, the mock leaving them zero. This slightly mixes a
  harness concern into the contract header. Accepted for now because it is
  documented and the mock is behaviourally untouched; if the contract's purity
  matters later, the hooks move to a harness-side wrapper without changing the
  nine calls. Recorded so the mix is a decision, not an accident.

## Result (run of 2026-09-05)

Built and certified. All nine conformance groups pass against substrate N
(`run.sh --substrate n`) and against the mock unchanged (`run.sh`), stable
across repeated runs, zero warnings under `-Wall -Wextra`. The D2 boundary was
not reached: the real cross-process `as_clone` closed through the harness on the
first full run.

Each call uses its real N mechanism, verified at the source and by negative
control: `VirtualAlloc`/`VirtualProtect`/`VirtualFree` for the address space;
`RtlCloneUserProcess` for `as_clone`, the child self-certifying copy-on-write
and reporting through its exit status; `CreateThread` + `SuspendThread` +
`Get`/`SetThreadContext` for the thread calls, red zone whole; the real `%gs`
carrier (DR-0003/DR-0021), not the mock's side table, for `tp_set`; a
fault-guarded copy for `user_copy`. Two negative controls confirm the suite
bites: biasing the carrier read fails only `tp_set`; corrupting the child's
pre-clone pattern fails `as_clone` through its cross-process verdict.

The substrate leaf is real. What sits on it next is the core's Phase 1 (0011
§ 18): `binfmt_elf`, the initial stack and auxv, the vDSO, the gate dispatch,
`exit_group` — a static program that prints and exits through the gate. That is
trunk-on-leaf and a separate run.
