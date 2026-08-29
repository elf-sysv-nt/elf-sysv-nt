# Proposal 0002 — the TLS model is a runtime-owned thread pointer through %gs

Status: accepted
Author: Philip Dye
Date: 2026-08-29
Analysed against: 0e3246a on `main`

The thread pointer is a word this runtime owns, kept below the thread's stack
base in the shape Cygwin's `_my_tls` already uses, and reached through `%gs`.
That is carrier C3 of `spike/gs-thread-pointer/`. It is an operator decision,
the second of the three `AGENTS.md` reserves, and it is taken against the
spike's measurement rather than ahead of it: the transcript is
`spike/gs-thread-pointer/results-2026-08-29.txt` and it ran the same day.

## Context and scope

Spike 1 refuted ELF-standard `%fs`-relative TLS on this host on 2026-08-29: a
user-written FS base comes back zero after any deschedule, and even under bare
preemption with no system call in sight. That took the psABI's native TLS path
off the table and left a hole the toolchain layer has to fill, not merely more
work to schedule. `AGENTS.md` reserved the choice of filler for the operator
and named none.

The gs-thread-pointer spike is what turned that reservation into something to
choose between. It measured four carriers side by side — a fixed `TlsSlots`
index (C1), the PE TLS directory (C2), a word below the stack base (C3), and
`NtTib.ArbitraryUserPointer` (C4) — against spike 1's twelve persistence cases
plus addressing, contention, lifecycle, and cost. This proposal reads that
transcript and settles which carrier the runtime is built on.

In scope: the carrier, and the reasons the other three are not it. Out of
scope: the access-sequence codegen WP-30 emits, the specs-file default WP-13
bakes in, and the loader-side static-block sizing WP-37 owns. Those consume
this decision; they are not part of it.

## Goals and non-goals

Goals. Name the carrier its consumers can cite rather than re-derive. Record
why it wins over the three it beat, including the one that also passed, so the
choice is legible as a trade rather than a preference. State what the decision
costs to reverse.

Non-goals, each of which could reasonably have been a goal:

- Writing WP-30's body. The carrier decides the interface's meaning, not its
  code; the codegen that emits the `%gs`-chain is a work package, gated on this
  and on WP-22, and doing it here is how the decision and its implementation
  come to disagree.
- Choosing the access model for every TLS dialect. Local-exec, initial-exec,
  general-dynamic and local-dynamic each reach the block differently once the
  thread pointer is in hand; that is WP-30 and WP-37's shared surface, and it
  is the same surface whatever carrier holds the pointer.
- Re-measuring C3 against the real `_my_tls`. The spike measured a stand-in and
  said so; the real block wants re-measuring inside WP-2x once `elfsysv1.dll`
  exists. That is a verification step this decision depends on downstream, not
  a reason to defer it.

## The decision

C3: the thread pointer lives in a word a fixed distance below the thread's
stack base, reached as `gs:[NtTib.StackBase]` then a fixed offset.

Three carriers passed the spike's persistence and addressing bar identically —
C1, C3 and C4 each returned the pointer they were handed across 17.6 billion
checks with zero failures, through the 45 million context switches the load
window actually performed, and each read a glibc-shaped block back correctly at
all four probed places. Persistence did not separate them, so the decision
rests on ownership, precedent and lifecycle, where they differ sharply.

C3 wins on precedent and ownership together. This project is derived from
Cygwin and re-faces `cygwin1.dll` rather than replacing what is behind it, and
`_my_tls` is the mechanism Cygwin already keeps at exactly this location by
exactly this chain. Choosing C3 reuses the one carrier with a working precedent
on this platform and an owner the runtime controls, rather than introducing a
new one. The block is the runtime's own memory: nothing else writes below its
stack base at its padding, so the collision hazard that disqualifies C1 does
not arise, and the between-builds hazard that disqualifies C4 does not either.

## Why not the others

C1, a fixed `TlsSlots` index, is out as written. A hardcoded index and
`TlsAlloc` draw from the same 64 slots, so a collision is a matter of
construction, not luck — and an injected endpoint-protection DLL calling
`TlsAlloc` is a deployment reality `AGENTS.md` already records. The spike's
contention case priced the slack rather than removing the hazard: the lowest
free index was 3 at start and unmoved after ten DLLs, which is room, not
safety. A `TlsAlloc`-at-startup variant is the safe version, but it trades C3's
owned block for a dynamic index that must itself be stored somewhere reachable
before the first TLS access — machinery C3 does not need. It is the fallback,
not the choice.

C4, `NtTib.ArbitraryUserPointer`, passed every case identically and is still
out, on ownership. The field is undocumented and Microsoft can repurpose it
between builds; a clean persistence table on one build says nothing about the
next, and the spike cannot measure that risk, which is precisely what makes it
disqualifying for the foundation of the whole TLS layer. The spike carried C4
to dismiss it with evidence rather than taste, and the evidence is that it
works and still cannot be trusted to keep working.

C2, the PE TLS directory, is unavailable rather than rejected. The pinned
toolchain (Cygwin gcc 7.4.0) emits emulated TLS, so the image carries no PE TLS
directory, `_tls_index` does not resolve, and the loader populates no slot at
`gs:[0x58]`. The chain cannot be built by a Cygwin-compiled object as things
stand. It is off the table until the runtime is built with native TLS, which is
not how Cygwin builds today, and reopening it is a different program of work.

## What it costs against ELF, and to reverse

C3 is a compromise on the access path and it is worth naming as one. Native
ELF reaches a thread-local in a single `%fs:offset` op because the base is the
thread pointer; here `%gs` is NT's TEB, so every access is irreducibly a load
of the pointer out of the carrier followed by an offset off it — one extra load
and a register per site, about two to three cycles over a global in the spike's
cost case (C3 at 5.5 against a global at 2.5). What survives intact is the ELF
object format and the glibc TCB layout: the spike's addressing case confirmed
`tcbhead_t` at the thread pointer, the guard at TP+0x28, and the static block at
negative offsets all read back correctly, so only how the pointer is *fetched*
changes, not how the block is *shaped*. The comparison that decides it is not
carrier-versus-native, which is unavailable, but carrier-versus-emulated-TLS,
the only other working option: emutls measured 33.7 cycles per access on this
same toolchain, six times C3.

Reversing the decision is cheaper than the triple's. The carrier is named in
WP-30's codegen and WP-37's loader, and consumed by the specs default in WP-13;
changing it before those are written costs nothing, and after them costs a
toolchain rebuild and a loader change but touches no built package's on-disk
layout the way the triple does. The reopen condition is evidence from WP-2x that
the real `_my_tls` does not behave as the stand-in did — that its padding
constant, or its handling under an alternate signal stack, breaks the chain the
spike measured.

## Alternatives considered

Emulated TLS throughout, `__tls_get_addr` for every access. It is the one model
that needs no carrier decision at all, works today, and is six times slower per
access than any `%gs` carrier. It stays the floor a carrier is measured against,
not the plan.

`TlsAlloc`-at-startup C1. The documented, supported form of a `TlsSlots`
carrier, and the second choice on this table. It is declined only because C3's
Cygwin precedent makes the owned-block form lower-risk here; on a runtime not
derived from Cygwin it would likely lead.

Masquerading the thread pointer as a `%fs` base via a per-access `wrfsbase`.
Ruled out by spike 1 directly: the base does not survive the scheduler, and
there is no call site on the preemption path to re-establish it at.

## Cross-cutting concerns

Nothing is built yet, so there is no migration and rollback is editing this
decision's consumers, none of which exist. The one carried dependency is the
WP-2x re-measurement of the real `_my_tls`; until it runs, this decision rests
on a stand-in that exercised the chain and the lifecycle hooks but not Cygwin's
actual block, its padding, or its alternate-signal-stack behaviour. That is
named here so it is not mistaken for settled.

## Verification criteria

1. `spike/gs-thread-pointer/t/run-tests.sh` passes, including the negative
   control that fails every carrier under `-DSPIKE_BREAK_CARRIER`.
2. `spike/gs-thread-pointer/results-2026-08-29.txt` reports `C3=pass` and a
   `shape` line in which every `C3/...` pair reads `pass`.
3. `doc/decisions/0003-tls-model.md` exists, is rowed once in
   `doc/decisions/index.md`, and that row points back at this proposal.
4. The governing documents that carried the TLS model as an open decision —
   `AGENTS.md`, `doc/ROADMAP.md`, `doc/IMPLEMENTATION-PLAN.md`,
   `doc/milestones.md`, `doc/elf-technical-breakdown.md`,
   `doc/elf-userspace-execution.md` — read it as settled and cite DR-0003.
5. `git ls-files -s` reports mode 100644 for every document added.

## Open questions

None that block the decision. The one that follows it belongs to WP-2x: whether
the real `_my_tls`, its padding constant, and its behaviour when Cygwin moves a
thread onto an alternate signal stack match what the stand-in measured. If they
do not, the reopen is a new record pointing back at DR-0003, not an edit to it.
