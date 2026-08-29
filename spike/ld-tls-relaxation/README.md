# Does the linker emit `%fs`-relative code on its own?

Yes, and DR-0003 does not account for it.

Ran 2026-08-29 against the binutils 2.42 built for the triple in WP-12, on the
RHEL-8.10 emulation. `results-2026-08-29.txt` is the transcript and
`measure-relaxation.sh -B <bindir>` regenerates it.

## Why anyone asked

DR-0003 replaced `%fs`-relative TLS with a runtime-owned pointer reached
through the `%gs` chain, and its cost paragraph says the ELF object format and
the glibc TCB layout are untouched, that only how the pointer is fetched
changes. The list of places it says the carrier is named runs WP-30's codegen,
WP-37's loader, WP-13's specs default. No linker.

That reads as though the linker were a bystander, and WP-12 was built on that
reading: stock binutils, no port, ten acceptance claims green. The claims were
about symbol versioning and the header bytes, and none of them touches TLS.

## What it found

The linker rewrites the psABI's TLS access sequences in place, and the
instruction bytes it writes are its own. Handed a general-dynamic sequence and
an initial-exec sequence for a symbol it can resolve locally, `ld` relaxed both
to local exec and emitted:

    mov    %fs:0x0,%rax
    mov    %fs:(%rax),%rax

Neither instruction appears in the input. The first replaced a call to
`__tls_get_addr`; the second replaced a GOT load. On this host that register
addresses correctly and then reads as zero after anything that deschedules the
thread, which is exactly what spike 1 refuted.

So the affected component is `bfd`, in `elf_x86_64_relocate_section`, and the
affected work package is WP-12 rather than WP-13. The compiler can be taught to
fetch a thread pointer any way we like; it cannot stop the linker from
substituting its own fetch afterward.

## The size constraint, which is the awkward part

Relaxation is an in-place rewrite into the bytes the original sequence
occupied, and the psABI sizes those sequences so that the local-exec form
fits. General dynamic is sixteen bytes and the `%fs` local-exec form ld writes
is sixteen bytes, padding included.

A `%gs`-chain fetch does not fit in sixteen. It needs a load of `NtTib.StackBase`
out of `%gs`, a load of the pointer at the carrier's offset below it, and then
the variable's own offset: three instructions against two, and no room. So
"patch the relaxations to emit the new carrier" is not a drop-in substitution
even before anyone decides whether it is wanted.

## What this does not settle

Whether to patch the relaxations, disable them for this target, or arrange
that no TLS relocation ever reaches the linker because WP-13 stops emitting
them. That is a decision adjacent to the one `AGENTS.md` reserves, it changes
what WP-13 is allowed to emit, and this spike stops at the boundary rather
than choosing.

What it does establish is that the choice exists and belongs to somebody. A
toolchain shipped without making it produces binaries that link clean and
read a thread pointer that Windows has zeroed, at whatever later moment the
thread happens to be descheduled — the same debugging shape the red zone has,
and for the same reason.

## Not verified

That disabling the relaxations is sufficient. The initial-exec sequence in the
input carries `movq %fs:(%rax), %rax` written by the assembler, not by ld, so
a linker that relaxes nothing still passes that instruction through. Whoever
takes the decision above should assume the compiler side needs work regardless
of what the linker does.

Whether `ld` has a switch that already suppresses this. Nothing was looked for;
the measurement was of what happens by default, which is what a build gets.
