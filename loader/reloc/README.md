# WP-34 — relocation

This package turns a placed object graph into one whose parts point at each
other. WP-31 validated each object, WP-32 mapped it, and WP-33 walked the graph
of objects a program needs; this stage reads the relocation tables out of each
mapped image and writes the computed values back through it, so a call through
the PLT reaches the body that defines it and a data reference reaches the datum.
Producing a symbol lookup that a fuller loader would (GNU and SysV hashing,
scope and interposition rules, versioning) is WP-35 and WP-36; this package
resolves only what a relocation needs and draws that line explicitly below.

## What it carries

The relocation set is measured, not guessed. Reading the pinned Rocky 8.10
glibc and its companions (the same objects WP-51 pins) with the cross
toolchain's `readelf`, the `R_X86_64_*` types a real el8 object contains are:
`RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`, `IRELATIVE`, `TPOFF64`, `64`, `COPY`, and
`DTPMOD64`. The engine computes each of those, plus `DTPOFF64` for
completeness, plus `RELR`, which el8 does not emit but a newer toolchain can.

`RELA` is used throughout: every relocation the engine reads is an
`Elf64_Rela`, and an object that declares its PLT relocations as `REL` rather
than `RELA` is refused rather than misread. Both binding disciplines are
carried. Under `BIND_NOW` — signalled by `DF_BIND_NOW`, `DF_1_NOW`, or
`DT_BIND_NOW` — every PLT slot is resolved before the image runs. Under lazy
binding each PLT slot is left pointing back into the PLT, its GOT word biased so
the stub reaches the resolver, and the first call trips a trampoline that binds
it. `IRELATIVE` relocations run their resolver and store the body it returns;
they are applied last, after everything else, so a resolver sees a fully
relocated world, which is the order a real loader uses.

`elf_reloc.h` is the contract. `elf_reloc_add` records one placed object and
reads its dynamic view once — the string and symbol tables, the hash table it
sizes the symbol count from, the `RELA`, `RELR`, and PLT relocation tables, and
the binding flags — translating every dynamic pointer from a link address to a
runtime one by adding the load bias. `elf_reloc_apply` then relocates the whole
scope and freezes each object's `PT_GNU_RELRO` through the WP-32 hook once the
writes through it are done.

## Symbol resolution, and where it stops

A relocation like `JUMP_SLOT` or `GLOB_DAT` names a symbol, and the engine has
to find its definition to compute the value. It does the minimum: a scan of the
scope in load order that takes the first strong definition, remembering a weak
one as a fallback, and running an `STT_GNU_IFUNC` definition's resolver to the
body it chooses. A `COPY` relocation searches every object but the one importing
the datum, as it must. This is the default resolution order a fuller lookup
refines rather than contradicts. The hashed lookup, `RTLD_GLOBAL` promotion,
`LD_PRELOAD` interposition, and symbol versioning are WP-35's and WP-36's; this
package deliberately does not reach into them. The symbol count each scan needs
comes from `.hash`'s `nchain` when the object carries one, and from a walk of
`.gnu.hash` otherwise.

## The lazy resolver crosses an ABI boundary

Lazy binding on this platform is the same mechanism glibc's
`_dl_runtime_resolve` is, with one crossing added. The mapped objects run the
System V AMD64 ABI; the loader and the fixup it calls are host (Cygwin) code on
the Microsoft ABI. So `reloc_resolve.S` saves the System V argument and scratch
registers the interrupted call still holds, calls `elf_reloc_fixup` with its two
arguments in `%rcx` and `%rdx` and 32 bytes of shadow space, takes the resolved
target, restores the registers, drops the cookie and index the PLT pushed, and
tail-jumps to the target. The cookie in `GOT[1]` is the `elf_reloc_object` the
engine stored; `GOT[2]` is the trampoline. This is the same floor DR-0000
describes: the loader is written face-side and enters mapped System V code from
a Microsoft-ABI host.

## TLS, and what this platform will not build

The engine computes the static-TLS relocations `TPOFF64`, `DTPMOD64`, and
`DTPOFF64` against the initial/local-exec layout a real loader assigns at
relocation time: x86-64 is TLS variant II, so each module's block is rounded to
its alignment and stacked below the thread pointer, and a `TPOFF64` is the
symbol's offset within its module plus the addend plus that module's negative
distance from the pointer. Standing up a live thread pointer is not this
package's — it is WP-40 and WP-41's, and DR-0003's `%fs` finding (recorded in
DR-0000) is why `%fs` cannot carry one on this host.

That same finding means this platform's own toolchain refuses to emit a TLS
relocation from source: WP-12 turns away the `%fs` relocations at link, and the
cross linker fails outright on both the GOT (`GOTTPOFF`) and the plain-data
(`@tpoff`) forms. A `TPOFF64` therefore only exists in a prebuilt vendor object,
never in one built here. The certification uses that fact rather than fighting
it: the TLS relocations are held to the real el8 `libc.so.6`.

`RELR` is a similar case from the other direction. This binutils only packs
relative relocations into `RELR` when it can add a `GLIBC_ABI_DT_RELR` version
dependency, which needs a glibc to link against; with the freestanding
specimens here it silently emits none, and el8 carries none in the first place.
The `RELR` decoder is therefore factored out as `elf_reloc_relr` and certified
over a constructed stream.

## Certification

`t/run.sh` builds the engine and its scaffolding with the host compiler, builds
the dynamic specimens with the cross toolchain, and holds the engine to the
done-when bar over them. The specimens carry no libc: they are entered at
`e_entry` with a handshake pointer in the first argument register, the private
convention WP-32's test established, and make no system calls.

- **hello, both ways.** A PIE that imports a function and a datum from
  `libgreet.so`, follows an internal relocated pointer, and calls an
  ifunc-dispatched `memcpy`. WP-33 walks its graph, WP-32 maps each object, and
  the engine relocates the scope. Linked lazy, the PLT slot for the imported
  call still points into the PLT before the run and at the callee after it, so
  the resolver trampoline is what bound it; linked `BIND_NOW`, it points at the
  callee before the image runs. Both variants run and report a correct
  cross-object call, a correct imported datum, a correct internal pointer, and a
  correct memcpy.

- **the ifunc.** The specimen's `memcpy` resolver picks its body on the CPUID
  ERMS bit, one of the criteria glibc's own `memcpy` ifunc tests. The engine
  runs the resolver through the `IRELATIVE` relocation, and the body it lands on
  is the one the same criterion selects when the harness evaluates it natively —
  the same implementation a real loader selects on this CPU.

- **RELR.** The decoder is run over a constructed stream mixing both entry
  forms — an address word and a 63-word bitmap — and exactly the named words are
  relocated.

- **TLS over a real object.** The pinned `libc.so.6` is mapped and its
  self-contained relocations applied by `elf_reloc_apply_bootstrap` — the subset
  an object satisfies against itself, without a resolved scope and without
  running any glibc code. Every relocation type the object carries is one the
  engine implements, and all eighteen of its `TPOFF64` relocations land at the
  offset the static-TLS layout dictates.

Run it from the package's `t/` directory:

    ./run.sh            # build and certify, in a scratch dir
    ./run.sh -k         # keep the built binaries

The TLS case needs the pinned vendor `libc.so.6`. It is found automatically
under `/tmp/wp34-vendor` when WP-51's `fetch-vendor.sh` has unpacked it there,
or its path can be given in `WP34_VENDOR_LIBC`; absent it, that one case is
skipped and the rest still run.

## What it does not do

It does not look symbols up by hash, order overlapping scopes, honor
interposition, or match symbol versions — those are WP-35 and WP-36. It does not
stand up thread-local storage for a running thread, only compute the
relocations that describe it — that is WP-40 and WP-41. And it makes no system
calls of its own: it writes through memory WP-32 already placed and committed.
