# DR-0003 — the TLS model is a runtime-owned thread pointer through %gs

Status: accepted
Date: 2026-08-29
Deciding: the operator
Proposal: `doc/proposals/0002-tls-model.md`

## What was decided

The thread pointer is a word this runtime owns, kept a fixed distance below the
thread's stack base and reached as `gs:[NtTib.StackBase]` then that offset —
carrier C3 of `spike/gs-thread-pointer/`, the shape Cygwin's `_my_tls` already
uses. TLS accesses fetch the pointer through that `%gs` chain and then address
the block relative to it; the block keeps its glibc layout, with `tcbhead_t` at
the thread pointer and the static block at negative offsets.

`AGENTS.md` reserves this decision for the operator, who took it on 2026-08-29
against the gs-thread-pointer transcript rather than ahead of it.

## Why C3 and not the other carriers

The spike measured four carriers on spike 1's twelve persistence cases plus
addressing. Three passed identically — a fixed `TlsSlots` index (C1), the word
below the stack base (C3), and `NtTib.ArbitraryUserPointer` (C4) — each
returning its pointer across 17.6 billion checks with zero failures, through 45
million real context switches, and each reading a glibc-shaped block back
correctly at all four probed places. Persistence did not choose between them.
Ownership and precedent did.

C3 is the mechanism Cygwin already keeps at this location by this chain, and
this project re-faces `cygwin1.dll` rather than replacing it, so C3 reuses the
one carrier with a working precedent on this platform and a block the runtime
owns. That owned block is what disposes of the other two: nothing else writes
below the runtime's stack base at its padding, so C1's collision hazard does not
arise, and Microsoft cannot repurpose the runtime's own memory between builds,
so C4's ownership hazard does not either.

C1 is the fallback rather than the choice. A hardcoded index and `TlsAlloc`
draw from the same 64 slots, so a collision — including from an injected
endpoint-protection DLL, a deployment reality `AGENTS.md` records — is a matter
of construction. The contention case priced the slack (lowest free index 3,
unmoved after ten DLLs) rather than removing the hazard. The safe form is
`TlsAlloc` at startup, which trades the owned block for a dynamic index that
must itself be stored somewhere reachable early; on a runtime not derived from
Cygwin it would likely lead, and it remains the documented second choice here.

C4 passed every case and is still declined, on a hazard the spike cannot
measure: an undocumented TEB field can be reused between Windows builds. It was
carried to be dismissed with evidence, and it was.

C2, the PE TLS directory, is unavailable rather than rejected. The pinned
toolchain emits emulated TLS, so the image carries no TLS directory and the
loader fills no slot at `gs:[0x58]`; the chain cannot be built by a
Cygwin-compiled object until the runtime is built with native TLS, which is a
separate program of work.

## What it costs, and to reverse

Against native ELF this is a compromise on the access path. `%gs` is NT's TEB
rather than the thread pointer, so every access is irreducibly a load of the
pointer out of the carrier and then an offset — one extra load and a register
per site, about two to three cycles over a global (C3 measured 5.5 against a
global's 2.5). The ELF object format and the glibc TCB layout are untouched;
only how the pointer is fetched changes. The comparison that justifies the cost
is not native, which spike 1 removed, but emulated TLS, the only other working
option, which measured 33.7 cycles per access — six times C3.

Reversing is cheaper than reversing the triple. The carrier is named in WP-30's
codegen, WP-37's loader, and WP-13's specs default; changing it before those
exist costs nothing, and after them costs a toolchain rebuild and a loader
change but no built package's on-disk layout.

## When to reopen this

The spike measured a stand-in, not Cygwin's real `_my_tls`. Reopen if WP-2x
finds the real block behaves differently in a way that breaks the chain — its
padding constant, or its handling when Cygwin moves a thread onto an alternate
signal stack. That is the one carried risk, and it is a re-measurement WP-2x
owns, not an argument left open here.

Reopening means a new record pointing back at this one. Do not edit this one.

## Where it is written down

`AGENTS.md`, under the reserved decisions. `doc/ROADMAP.md`, the assumed-path
table. `doc/IMPLEMENTATION-PLAN.md`, WP-30 and its dependency line, and WP-13's
specs default. `doc/milestones.md`, spike 1. `doc/elf-technical-breakdown.md`,
in the TLS sections and the open-questions list. `doc/elf-userspace-execution.md`,
where the model was left to be defined.
