# DR-0102 — criteria 3 and 4 are amended, both substrates are offered, and the seam gets its teeth now

Status: accepted
Date: 2026-09-06
Deciding: the operator, on proposal 0012's open questions 2, 3, 5 and 6 (decisions of 2026-09-05)
Proposal: 0012
Amends: doc/design/Verification-Plan.md § The kernel's criteria

## What was decided

Four of proposal 0012's open questions, each a promise or a value the ladder
could not reach, answered by the operator.

Criterion 3 of proposal 0011 narrows to WSL. A tree written by the kernel is
read by WSL's DrvFs with modes, owners, symlink targets and special files
identical, and the reverse holds for a tree written by WSL. Cygwin leaves the
criterion: spike 39 measured Cygwin 3.6.10 reading neither the LX mode EAs
nor device nodes, and what Cygwin sees (every file 755 owned by the Windows
account, a device as an empty regular file) is recorded in the VFS design as
what a Cygwin reader of this tree gets.

Criterion 4 of proposal 0011 becomes per substrate. Under H the fork of a
200-VMA, 64-descriptor process completes with a median under 2 ms on the
operator's machine, spike 44's table copy having measured 0.5 to 0.75 ms at
576 MB. Under N the same fork completes, its median is recorded beside
Cygwin's on the same machine, and the check fails if the median exceeds 1.5
times the value recorded at certification on the same host; spike 35 measured
the only clone that runs a thread at 5.1 ms, and no threshold N could be held
to would be honest.

Both substrates are offered, and the client's environment decides. A client
whose desktop exposes no hypervisor gets N; a client that needs stock el8
binaries gets H; a client with both properties has neither and is told so.
0011 open question 1's second condition is answered: the hypervisor is a
prerequisite of H, never of the platform.

The process seam gets its teeth before phase 2: `bin/check-substrate-line`
refuses, in `core/`, the Win32 spellings of the named-object and port calls
beside the `Nt*` it already refuses, and `seam/` is the directory the two
realisations live in, outside the walk.

## Why

Each was tier 8 on the ladder, a change to what the project promises or a
value about who it serves, and each is the operator's. The measurements that
made them askable are spikes 35, 39, 44 and 45, and the arguments are 0012's
open questions with their survivors.

## Consequences

`Verification-Plan.md` gains § The kernel's criteria, stating that 0011's
verification criteria are the criteria of record as amended here, and cites
this record. 0011 carries a dated addendum pointing at it. `Architecture.md`
§ The shape of the system already states both substrates and the client's
choice, citing DR-0098; this record is the operator's confirmation. The
checker change and `seam/` land with this record.
