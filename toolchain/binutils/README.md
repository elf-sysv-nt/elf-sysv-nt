# binutils

There is almost no port. Every target pattern binutils matches on is written
`x86_64-*-linux-*`, so the canonical triple lands on the ordinary x86_64 ELF
target with nothing added to `config.bfd`, `configure.tgt`, or the ELF backend.
Configure, build, install, and 2.42 came up first time on 2026-08-29.

Two patches, both to the one place `bfd` writes a thread-pointer fetch of its
own. `patches/` has them.

    build-binutils -P <prefix>
    t/accept.sh -B <prefix>/bin

Fifteen claims, all green, covering four criteria.

## What accept.sh checks

The interesting ones are not the ones the plan named.

`.symver` survives assembly at both nodes, and `--version-script` produces a
`.gnu.version_d` that `readelf -V` prints with both nodes defined and the
parent chain recorded. That is three claims rather than one because the failure
being guarded against is not a linker that rejects the option. It is a linker
that accepts it, links clean, and silently drops the version names, which is
the trap `doc/history/symbol-versioning-formats.md` records for the PE route
and the reason the format changed at all. So the test reads the verdefs back
out instead of checking an exit code.

The header bytes match `doc/design/target-definition.md`: an ordinary object
gets `ELFOSABI_NONE`, an object carrying an ifunc gets promoted to
`ELFOSABI_GNU`, and the hand-assembled `.note.ABI-tag` reads back as Linux
3.2.0 in a `PT_NOTE`. Both halves of the OSABI rule are checked, because a test
that only saw the zero would prove the byte was zero rather than that it was
zero for a reason.

One claim was not in the plan and belongs to spike 4. `elfdeps` formats the
versioned `Provides` off the base verdef node and the unversioned one off
`DT_SONAME`, so a library whose two disagree generates dependencies that do
not match the vendor's. Passing `-soname libc.so.6` makes the linker write
`libc.so.6` into the base node by itself, which discharges half of spike 4's
condition on WP-53 at WP-12 prices.

## The TLS relaxations

`ld` writes `%fs`-relative thread pointer fetches of its own, out of `bfd`
rather than out of anything the compiler produced. Handed the psABI's general
dynamic and initial exec sequences it relaxes both to local exec and emits
`mov %fs:0x0,%rax`, on a host where spike 1 established that base does not
survive a context switch. `spike/ld-tls-relaxation/` has the measurement, and
DR-0003's list of places the carrier appears has no linker in it, which is why
this went unnoticed until the acceptance run was already green.

`patches/0001` answered with a refusal. The carrier DR-0003 chose needed
three instructions where the psABI reserves sixteen bytes for two, so no
in-place substitution existed, and a link error was the honest answer for an
input the toolchain could not translate. Where that check sat turned out to
matter more than what it checked: a first version placed it after
`elf_x86_64_tls_transition`, which rewrites the relocation type in place, and
local dynamic arrived as a form on the accepted list and went on to emit the
fetch. The test that caught it assembles one model per object.

`patches/0002` replaces the refusal with the rewrite, because DR-0101 moved
the carrier. `TlsSlots[63]` is one load, `mov %gs:0x1678,%rax`, and that
instruction is the same nine bytes as `mov %fs:0x0,%rax`: a segment prefix,
REX.W, the opcode, a SIB-absolute ModRM and a disp32. The room the psABI
reserved for the local-exec form holds it, so every relaxation now writes the
carrier's fetch where it wrote the psABI's, eleven byte strings in all, and
nothing else about the rewrites changes. General dynamic and local dynamic in
an executable relax to the `%gs` fetch; initial exec relaxes its GOT load to
an immediate as before; general dynamic in a shared object is left alone, a
call to `__tls_get_addr`. `t/accept.sh` reads each of those back out of the
disassembly, and separately checks that no `%fs` survives in any of them.

The refusal had also been wider than its reason. `GOTTPOFF`'s relaxation
writes an immediate and no segment prefix, and the `%fs:(%rax)` the spike saw
beside it was in the assembler's input, not the linker's output. Refusing it
was what stopped every static link against a glibc `libc.a` (whose own TLS is
initial exec) from linking at all. The fetch the compiler writes for that
model is the compiler's to get right (`toolchain/gcc/patches/0002`), and an
assembler-written `%fs` in a finished object is caught by
`toolchain/glibc/check-no-syscall`, which disassembles the product.

Tier 1 on the ladder: with the carrier one load, the refusal's only remaining
argument was a size constraint that no longer held, and a linker that refuses
the sequences glibc's own objects carry is not correct for this target.
DR-0063's "linker half" paragraph and DR-0024's descriptor note describe the
refusal and want amending; the toolchain README is not the place, and the
amendment is reported rather than made here.

## Not verified

That the eleven rewritten strings are all of them. They were found by
searching the source for the two byte patterns of the `%fs:0` fetch, and a
relaxation that spelled the fetch another way would have been missed;
`check-no-syscall` over a linked glibc is the check that would catch it.

That a vendor `.o` from an el8 archive now links into something that runs. It
carries these relocations legitimately and links, with the carrier's fetch
written where the psABI's would have been; what the rest of its text does with
`%fs` is the post-link check's to refuse.

That 2.42 is the right release. It was chosen over el8's 2.30 for RELR and a
decade of x86 fixes, and nothing has yet needed either.

That either patch is right for anything but this target. Both are
unconditional in `elf64-x86-64.c` rather than gated on a target vector, which
is fine for a cross binutils built for one triple and would not be acceptable
upstream.
