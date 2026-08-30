# DR-0033 — a fatal signal leaves an ELF core, written by the runtime

Status: accepted
Date: 2026-08-30
Deciding: WP-61
Proposal: none; taken while writing WP-61

## What was decided

When a signal with a terminating default disposition ends a process, the
runtime writes an ELF core file before the process exits, and that file is
what a person debugs. The core is an ET_CORE image for EM_X86_64 in the
layout the Linux kernel writes — NT_PRSTATUS, NT_PRPSINFO, NT_AUXV and
NT_FILE notes followed by PT_LOAD segments — so the WP-60 gdb reads it
through the same code that reads a core from a real Linux machine.

The Windows minidump remains what a crash leaves when it happens outside
the runtime's reach: in the stub before the runtime is up, or in host code
the delivery path never sees. `runtime/coredump/README.md` says how to work
from one. It is the fallback, not the deliverable.

## Why the runtime writes it

Because nothing else can. The kernel that would write a core on Linux is not
here, and the host's crash machinery produces a minidump of the wrong world:
a PE stub, anonymous executable regions, no mapping from either to the DWARF
the packages were built with. A minidump answers "what did Windows see", and
what Windows sees is the situation WP-60 already recorded as broken by
design. The one party that holds the ELF view of the process — the register
file the delivery path captured, the link map WP-39 laid down, the memory it
mapped itself — is the runtime, so the runtime is where the core is written.

The capture is already done by the time the question arises. DR-0030's
delivery hijack copies the interrupted thread's register file into an
`elfsysv_sigctx_t` before anything else runs, and `sigdisp` classifies the
signal against the process's dispositions. A fatal signal surfaces as the
`ELF_SIG_DEFAULT` disposition, which today no caller consumes: the
termination path is unwritten. That unwritten path is the seam. Whoever
writes it calls `elfcore_write` with the context in hand and then exits;
the writer asks nothing of the host and adds no capture of its own.

## The shape of the writer

`elfcore_write` takes a register file, a description of the process, and a
list of memory segments, and emits bytes through a caller-supplied sink. It
opens no file and walks no memory itself. The split is deliberate and is the
same one `signal.h` records for the delivery path: everything above the host
translation is driven by a plain record so a test can drive it without a
host thread — or here, without a crashing process. Collecting the segments
is the caller's side (under the stub: the link map plus `VirtualQuery`),
and it stays out of the writer so the bytes the tests certify are the bytes
a fault will produce.

The note layouts are written into the source as offset assertions against
the kernel's structures — prstatus at 336 bytes with the register file at
112, prpsinfo at 136 — because gdb hard-codes the same numbers. A drifted
struct fails the build, not the debug session.

## What was rejected

Writing a minidump and teaching the tools to read it: rejects the premise.
Every consumer downstream — gdb, crash-reporting scripts, a person with
thirty years of core-file habits — speaks ELF core, and the project's floor
presents ELF outward everywhere else. Translating minidumps forever is a
second face grown to avoid writing one file format once.

Converting a minidump to an ELF core after the fact: the minidump does not
contain the ELF view. The mapping from stub regions to ELF objects lives in
the runtime's link map, which is in the crashed process's memory; a
converter would re-derive from the outside what the runtime knows directly.

## Not verified

That gdb reconstructs the shared-library list from NT_FILE alone for a
process under the stub; certified so far against synthetic images, since the
loader cannot yet run a process to crash (the WP-15/WP-60 boundary). The
fatal-path wiring itself is unwritten and carries this decision when it is.
