# DR-0016 — relocation types the platform will not emit are certified against vendor objects

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: the WP-34 author, against the delivered toolchain and the pinned el8
objects
Proposal: certify the relocation engine's TLS and RELR support over prebuilt
vendor objects and a constructed stream, rather than over specimens built from
source, because the platform's own toolchain cannot produce those relocations.

## Context

WP-34 must apply the whole `R_X86_64_*` set el8 objects contain, which a reading
of the pinned Rocky 8.10 glibc and companions fixes as `RELATIVE`, `JUMP_SLOT`,
`GLOB_DAT`, `IRELATIVE`, `TPOFF64`, `64`, `COPY`, and `DTPMOD64`, and it must
carry `RELR` for a toolchain that emits it. The other WPs in the loader certify
against specimens built with the cross toolchain: WP-32 built static ELF
specimens, WP-33 built graphs. WP-34 cannot do that for two of the types.

A `TPOFF64` needs a thread-local reference, and the platform refuses those at
the link. DR-0003 found that a user-written `%fs` base does not survive a
context switch on this Windows, DR-0000 makes that the reason glibc's TLS cannot
run here, and WP-12 turns away the `%fs` relocations at link time. Measured
against the delivered cross linker, both TLS reference forms fail: the GOT form
`R_X86_64_GOTTPOFF` is refused with a message naming the `%fs` presumption, and
the plain-data `@tpoff` form trips a BFD assertion. There is no way to build an
object carrying a `TPOFF64` from source on this platform.

`RELR` fails from the other direction. This binutils only packs relative
relocations into `.relr.dyn` when it can add a `GLIBC_ABI_DT_RELR` symbol-version
dependency, which requires a glibc to link against; the freestanding specimens
here provide none, so `-z pack-relative-relocs` silently emits nothing. And el8
itself carries no `RELR` in any object, so there is no vendor one either.

## What was decided

The engine implements all of these types, and each is certified against the best
real evidence available rather than a specimen that cannot exist:

- `TPOFF64`, `DTPMOD64`, and `DTPOFF64` are held to the pinned el8 `libc.so.6`,
  which carries eighteen genuine `TPOFF64` relocations. The object is mapped and
  its self-contained relocations applied, and every stored offset must equal
  what the static-TLS layout dictates.

- `RELR` is factored into a single decoder, `elf_reloc_relr`, and certified over
  a constructed stream that exercises both entry forms.

- The rest — `RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`, `64`, `COPY`, and `IRELATIVE`,
  under both lazy and `BIND_NOW` binding — are certified against dynamic
  specimens the cross toolchain does build, since none of them touch `%fs` or
  need `RELR`.

To make the vendor-object TLS check possible without running any glibc code — a
lone `libc.so.6` cannot resolve the symbols it imports from `ld.so`, and running
its ifunc resolvers in a half-built world is not something this WP will do — the
engine grows a second entry point, `elf_reloc_apply_bootstrap`. It applies only
the relocations an object satisfies against itself: `RELATIVE`, `RELR`, and the
static-TLS trio. This is not a testing convenience bolted on; it is the subset a
real loader relocates first (glibc's `ELF_DYNAMIC_RELOCATE` does relative before
the rest), and it is a real capability the loader will use when it relocates
itself before its scope exists.

## Why not the alternatives

Building a from-source TLS specimen was tried and cannot be made to work here;
that is the finding, not a shortcut around one. Relocating the full
`libc.so.6`–`ld.so` closure would exercise more, but it means running vendor
ifunc resolvers against uninitialised glibc-internal state, which is a fault
waiting to happen and is WP-40/WP-41's territory once a real process image
exists. Skipping `TPOFF64` and `RELR` certification entirely would leave two of
the delivered types unproven. Certifying each against the strongest real
artifact available is the honest middle.

## Consequences

The engine carries a `elf_reloc_apply_bootstrap` entry point and a public
`elf_reloc_relr` primitive alongside the full `elf_reloc_apply`. The TLS
certification depends on WP-51's vendor tree being unpacked; when it is absent
that one case is skipped and the rest still run. Full TLS execution and the
full-closure relocation of real glibc remain WP-40 and WP-41's, which this
record does not decide.

## When to revisit

If the host ever preserves a user-written `%fs` base across a context switch —
the condition DR-0000 names for reopening the glibc question — the platform
could emit and run TLS from source, and a from-source `TPOFF64` specimen would
then be the better certification. Absent that, this stands.
