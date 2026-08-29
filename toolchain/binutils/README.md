# binutils

There is almost no port. Every target pattern binutils matches on is written
`x86_64-*-linux-*`, so the canonical triple lands on the ordinary x86_64 ELF
target with nothing added to `config.bfd`, `configure.tgt`, or the ELF backend.
Configure, build, install, and 2.42 came up first time on 2026-08-29.

One patch is needed, and it is a refusal rather than a port. `patches/` has it.

    build-binutils -P <prefix>
    t/accept.sh -B <prefix>/bin

Fourteen claims, all green, covering four criteria.

## What accept.sh checks

The interesting ones are not the ones the plan named.

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

## The TLS refusal

`ld` writes `%fs`-relative thread pointer fetches of its own, out of `bfd`
rather than out of anything the compiler produced. Handed the psABI's general
dynamic and initial exec sequences it relaxes both to local exec and emits
`mov %fs:0x0,%rax`, on a host where spike 1 established that base does not
survive a context switch. `spike/ld-tls-relaxation/` has the measurement, and
DR-0003's list of places the carrier appears has no linker in it, which is why
this went unnoticed until the acceptance run was already green.

So `patches/0001` refuses the relocations that license an instruction rewrite
rather than rewriting them differently. The `%gs` chain needs three
instructions where the psABI reserves sixteen bytes for two, so no in-place
substitution exists even if one were wanted, and a link error is the right
answer for an input this toolchain cannot honestly translate. `TPOFF32`,
`TPOFF64`, `DTPMOD64` and `DTPOFF64` stay accepted: values rather than
sequences, no instruction bytes written for them, and WP-13's codegen will
want `TPOFF` for sequences of its own.

Where the check sits turned out to matter more than what it checks. A first
version placed it after `elf_x86_64_tls_transition` and passed the general
dynamic and initial exec tests, because both arrive there already rewritten
into `GOTTPOFF`. Local dynamic arrives rewritten into a form on the accepted
list, so it linked, and the output carried `mov %fs:0x0,%rax`. The test that
caught it assembles one model per object; a single object carrying all three
would have stopped at the first refusal and reported success.

## Not verified

That refusing these five is sufficient for the toolchain as a whole. The
initial exec sequence carries `movq %fs:(%rax), %rax` written by the
assembler, so a linker that rewrites nothing still passes that instruction
through untouched. The compiler side needs work regardless, and that is
WP-13's.

That the refusal is right for a vendor object rather than merely honest. A
`.o` from an el8 archive carries these relocations legitimately and will now
fail to link, which is the intended diagnosis and not a repair.
`doc/proposals/0003-vendor-binary-tls-rewriting.md` is where the repair lives.

That 2.42 is the right release. It was chosen over el8's 2.30 for RELR and a
decade of x86 fixes, and nothing has yet needed either.

That the patch is right for anything but this target. It is unconditional in
`elf64-x86-64.c` rather than gated on a target vector, which is fine for a
cross binutils built for one triple and would not be acceptable upstream.
