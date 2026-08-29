# binutils

There is no port, and that is the result rather than an omission. Every target
pattern binutils matches on is written `x86_64-*-linux-*`, so the canonical
triple lands on the ordinary x86_64 ELF target with nothing added to
`config.bfd`, `configure.tgt`, or the ELF backend. Configure, build, install:
that is the whole of it, and it worked first time on 2026-08-29 against 2.42.

    build-binutils -P <prefix>
    t/accept.sh -B <prefix>/bin

WP-12 is not finished. Its three stated criteria are met, mechanized in
`t/accept.sh`, and green; a fourth was missing from them and the toolchain is
wrong without it. More on that below.

## What accept.sh checks

Ten claims, and the interesting ones are not the ones the plan named.

`.symver` survives assembly at both nodes, and `--version-script` produces a
`.gnu.version_d` that `readelf -V` prints with both nodes defined and the
parent chain recorded. That is three claims rather than one because the
failure being guarded against is not a linker that rejects the option. It is a
linker that accepts it, links clean, and silently drops the version names,
which is the trap `doc/symbol-versioning-formats.md` records for the PE route
and the reason the format changed at all. So the test reads the verdefs back
out instead of checking an exit code.

The header bytes match `doc/target-definition.md`: an ordinary object gets
`ELFOSABI_NONE`, an object carrying an ifunc gets promoted to `ELFOSABI_GNU`,
and the hand-assembled `.note.ABI-tag` reads back as Linux 3.2.0 in a
`PT_NOTE`. Both halves of the OSABI rule are checked, because a test that only
saw the zero would prove the byte was zero rather than that it was zero for a
reason.

One claim was not in the plan and belongs to spike 4. `elfdeps` formats the
versioned `Provides` off the base verdef node and the unversioned one off
`DT_SONAME`, so a library whose two disagree generates dependencies that do
not match the vendor's. Passing `-soname libc.so.6` makes the linker write
`libc.so.6` into the base node by itself, which discharges half of spike 4's
condition on WP-53 at WP-12 prices.

## What is outstanding

`ld` writes `%fs`-relative thread pointer fetches of its own, out of `bfd`
rather than out of anything the compiler produced. Handed the psABI's general
dynamic and initial exec sequences it relaxes both to local exec and emits
`mov %fs:0x0,%rax`, on a host where spike 1 established that base does not
survive a context switch. `spike/ld-tls-relaxation/` has the measurement.

The repair is to refuse the relocations that license an instruction rewrite —
`R_X86_64_TLSGD`, `TLSLD`, `GOTTPOFF`, `GOTPC32_TLSDESC`, `TLSDESC_CALL` —
rather than to rewrite them differently. Two reasons. The `%gs` chain needs
three instructions where the psABI reserves sixteen bytes for two, so an
in-place substitution is not available even if it were wanted; and a link
error is the right answer for an input this toolchain cannot honestly
translate, which is what makes hand-written assembly and stray vendor objects
visible instead of silent.

`TPOFF32`, `TPOFF64`, `DTPMOD64` and `DTPOFF64` stay accepted. Those are
values rather than sequences, the linker writes no instruction bytes for them,
and WP-13's eventual codegen will want `TPOFF` for sequences of its own.

## Not verified

That refusing those five is sufficient. The initial exec sequence carries
`movq %fs:(%rax), %rax` written by the assembler, not by `ld`, so a linker
that relaxes nothing still passes that instruction through untouched. The
compiler side needs work regardless of what the linker does, and that is
WP-13's.

That 2.42 is the right release. It was chosen over el8's 2.30 for RELR and a
decade of x86 fixes, and nothing has yet needed either.

Whether the acceptance claims still hold once the TLS refusal lands. They were
measured against stock 2.42 and the patch has not been written.
