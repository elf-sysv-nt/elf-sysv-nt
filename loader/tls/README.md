# loader/tls — TLS in the loader (WP-37)

This package stands up ELF thread-local storage at run time. WP-30 established
the thread pointer and the TCB — carrier C3 of DR-0003, a runtime-owned word
reached through `gs:[NtTib.StackBase]`, with the psABI variant II TCB at the
pointer and `static_size` bytes of static block below it. WP-34 computes the
static-TLS relocations `TPOFF64`, `DTPMOD64`, and `DTPOFF64` but stops at the
arithmetic. This package is the rest: sizing the static block from the initial
`PT_TLS` set, building the DTV a thread reads through `tcbhead_t.dtv`, resolving
`__tls_get_addr`, and tearing the per-thread arrangement down.

## The four models against one layout

x86-64 has four TLS access models, and all four resolve against the single
static layout this package computes.

- **Local-exec** and **initial-exec** are tp-relative offsets. The executable's
  own TLS (LE) and a shared object present at startup (IE) each get a fixed
  negative offset below the thread pointer; a reference is one load of
  `tp + offset` and costs nothing at run time. Those offsets are exactly what
  WP-34 hands a `TPOFF64` — `elf_tls_static_tpoff` returns the same value — so a
  relocation WP-34 computed reads the datum this package placed.
- **General-dynamic** and **local-dynamic** go through `__tls_get_addr`, called
  with a `tls_index{ti_module, ti_offset}` the compiler emitted and a
  `DTPMOD64`/`DTPOFF64` pair filled. For a module in the static block it returns
  the very address the IE/LE offset names; for a module that is not, it
  allocates the block lazily, once per thread, off the DTV. Local-dynamic is the
  same call taken once for a module's base with per-datum offsets added off it.

## Variant II sizing, and the surplus

The static block is laid out downward from the thread pointer, each module's
size rounded up to its alignment and stacked below the last, the first module
nearest the pointer. This is the same rule WP-34's `assign_static_tls` uses, so
the offsets agree; the only thing this package adds to the block is a **surplus**
below the initial modules — room reserved so a module `dlopen`'d later can still
take a static (initial-exec) offset in a thread created before it. The surplus
is a tunable with a default, glibc's historical 1664 bytes, not a constant;
DR-0024 records why, and the plan flags getting it too small as a run-time
failure inside a library the program did not know it would load.

## The DTV, and a dlopen into a prior thread

The DTV is glibc's shape: `dtv[0]` is the generation the thread's vector was
last reconciled to, `dtv[-1]` its length, and `dtv[i]` module `i`'s block or an
unallocated marker. Registering a module bumps a generation counter in the
shared module table. A thread whose vector predates that bump learns it on the
next `__tls_get_addr`: the generations differ, so the vector is grown, the new
slots filled, and the accessed module's block allocated. That is the mechanism
behind the done-condition's hard case — a `dlopen`'d module with its own
`PT_TLS` resolving in a thread that existed before the `dlopen`.

Teardown frees the per-module dynamic blocks the DTV holds and the DTV itself,
leaving the static slots — which live in the TCB — for `elfsysv_tp_free`.

## What this package does not carry

TLS descriptors (`TLSDESC`) are not implemented: WP-12's binutils refuses the
descriptor relocations at link (`spike/ld-tls-relaxation`), so no object this
toolchain produces carries one. The `dlopen` surface itself — initialization
order, assigning a static offset to a late initial-exec module, `dlclose`
reclaiming a removed module's per-thread blocks — is WP-38's. DR-0024 draws
both lines.

## Certification

`t/tls_test.c`, built with the host gcc that targets `x86_64-pc-cygwin` and run
by `t/run.sh`, holds the package to the done-when bar in the synthetic style
WP-36 uses. Phase 1 lays out two `PT_TLS` modules with init images on a driven
thread pointer and asserts the static offsets are correct and aligned, that an
IE/LE address reads the init value (and a `.tbss` tail reads zero), that
`__tls_get_addr` returns the static module's own address and a lazily allocated
correct block for a dynamic one, and that all four models resolve. Phase 2 uses
the live `%gs` carrier: it creates a managed thread, registers a third module
after it, and has the thread resolve the new module through the real
`__tls_get_addr`, then tears the DTV down.
