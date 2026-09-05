# DR-0098 — proposal 0012 is ratified: three layers, one kernel process under H, the interface's object, and the I/O rule

Status: accepted
Date: 2026-09-05
Deciding: the operator, by the grant that accepted proposal 0012 ("proceed as
you deem fit", after the review of 0011 and the topology note)
Proposal: 0012
Amends: doc/design/Architecture.md § The shape of the system

## What was decided

Four things, each a section of proposal 0012 and each now stated in
`Architecture.md`.

The core sits on two seams, not one. The substrate interface is how user code
runs; the process seam is where state that crosses Linux processes lives (pid
table, wait, signal send, ttys, IPC namespaces, locks, shared futexes, and
`SIGKILL`/`SIGSTOP`/`SIGCONT` against an unresponsive process). The core calls
the seam as an interface; its realisation is chosen with the substrate. Under
N it is the supervisor 0011 designed, over ALPC; under H it is tables in the
kernel process (0012 § 1).

Substrate H is one kernel process per user per installation root, a Win32
process holding one partition, with a page-table root per Linux process and a
pool of vCPUs handed between tasks. Fork is the kernel's own page-table copy
with copy-on-write resolved on exception exits; `execve` is a new root; guest
physical memory is the kernel process's address space mapped identity and
lazily (0012 § 2).

A `struct substrate` is one address space and its threads, one per Linux
process; the nine calls and the C form are unchanged, and the prose of
`Substrate-Interface.md` now says what the C form said (0012 § 3).

The kernel never hands a host call a user address: every byte between user
memory and a host object goes through `user_copy_*` and a kernel buffer (0012
§ 4).

## Why

Each is settled by the ladder's first tier after a measurement, and the
proposal's decision log names the tier per item. One mapped partition per
process (spike 43) removed the shape in which each Linux process is a
partition inside the kernel process; the process-clone shape (spike 45) works
and loses to the table copy on fork (spike 44) and on `vfork` (spike 46). The
lazy, unpinned mapping (spike 42) is what lets H's memory be reserved host
ranges committed on exits. The interface's object was found in
`substrate/substrate.h` rather than invented. The I/O rule has one correct
candidate: under N a lazily committed user buffer fails the I/O manager's
probe, under H no host call can take a guest address.

## What it costs

Under H a syscall is about 5 µs, a first touch of fresh memory about 13 µs
unless populated ahead, a copy-on-write fault about 25 µs, and every Linux
process of a user shares one kernel process's fate. Under both, one `memcpy`
per I/O. These are recorded as the price, not as divergences.

## What it does not decide

The TLS carrier under N (0011 § 6 against DR-0003), criteria 3 and 4, 64 KB
against 4 KB under N, and the hypervisor as a prerequisite: 0012's open
questions 1 to 5, each the operator's.

## Consequences

`Architecture.md` § The shape of the system carries the layers, H's shape and
the I/O rule and cites this record; § The address space cites it for H's
memory beside DR-0014. `Substrate-Interface.md` gains § The object.
`README.md` and `AGENTS.md` are rewritten for the design of record in the
same change (0012 § 10). `bin/check-substrate-line` is to grow the ALPC and
named-object symbols for `core/` when phase 3 opens, per 0012 open question 6.
