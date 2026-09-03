# DR-0024 — the loader's static-TLS surplus and DTV shape, reproduced from the spec

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-37 implementation, a defensible reading under the non-reserved
decision policy in AGENTS.md; the operator may ratify or reopen
Proposal: none; taken while building the loader's TLS

## What was decided

WP-37 stands up the runtime side of ELF TLS on the thread pointer DR-0003 chose
and the static-TLS arithmetic WP-34 already computes. The layout, the DTV, and
`__tls_get_addr` are written from the generic ABI, the x86-64 psABI's variant II,
and Drepper's TLS account, not lifted from glibc's `dl-tls.c`, which is LGPL and
assumes a Linux loader this platform does not have (DR-0000, DR-0004). What it
reproduces is the observable behaviour. These are the readings that were choices
rather than transcription.

The surplus is a tunable with a default of 1664 bytes. The static block sized
from the initial `PT_TLS` set carries extra room below the initial modules so a
module `dlopen`'d later can still take a static offset in a thread created
before it existed. The plan flags this as a tunable, not a constant, and names
getting it wrong as a run-time failure inside a library the program did not know
it would load. The default is glibc's historical `TLS_STATIC_SURPLUS`,
`64 + DL_NNS * 100` with the sixteen namespaces glibc ships, i.e. 1664 bytes.
Reusing that number rather than inventing one means a program that runs under
glibc with the default surplus has the same headroom here. It is
`ELF_TLS_SURPLUS_DEFAULT`, and `elf_tls_state_init` takes an override.

A `dlopen`'d module takes the general-dynamic path, not the surplus. The
surplus exists so a late module *compiled* for initial-exec has a static offset
to occupy; a module reached through `__tls_get_addr` does not consume it, and
its block is allocated lazily on first access in each thread. So the surplus
bounds late initial-exec, and general/local-dynamic is unbounded by it. The two
mechanisms are separate, and this package implements the general-dynamic one in
full and leaves the static-offset assignment for a late initial-exec module to
WP-38's `dlopen`, where the object's relocations are known.

The DTV is glibc's shape, to the negative indices. `dtv[0].counter` is the
generation the thread's vector was last reconciled to; `dtv[-1].counter` is its
length; `dtv[i]` is module `i`'s block or `TLS_DTV_UNALLOCATED`. A thread
learns its vector is stale by comparing `dtv[0]` to the module table's
generation, which is bumped on every add. This is the mechanism that lets a
thread created before a `dlopen` resolve the new module: the access finds a
newer generation, grows the vector, and allocates the block. Keeping the exact
indices matters because `tcbhead_t.dtv` at head offset `0x08` is read by code the
compiler emits, and a differently shaped vector behind that pointer would need a
different `__tls_get_addr` than the one the psABI names.

TLS descriptors are not implemented. The plan lists them "if the toolchain
emits them." WP-12's binutils refuses `GOTPC32_TLSDESC` and `TLSDESC_CALL` at
link (`spike/ld-tls-relaxation`), because the `%gs` chain needs three
instructions where the psABI reserves sixteen bytes for two, so no object this
toolchain produces carries a descriptor relocation for the loader to service.
Writing a descriptor resolver now would be dead code certified against nothing.
If a future toolchain layer emits descriptors, this is where the resolver lands,
and reopening means a record pointing back here.

## What it does not decide

The `dlopen` surface itself — initialization order, the static-offset
assignment for a late initial-exec module, `dlclose`'s reclamation of a removed
module's per-thread blocks — is WP-38's. This record fixes how the static block
is sized, what the DTV is, and what `__tls_get_addr` does; it does not sequence
the loader around them. Teardown here frees a thread's dynamic blocks and its
DTV; the ten-thousand-cycle leak bar is WP-38's to hold.

## Where it is written down

`loader/tls/elf_tls.h` and `.c`, whose comments cite this record at the choices
above, and `loader/tls/README.md`. The behaviour is held to the checks in
`loader/tls/t/`.
