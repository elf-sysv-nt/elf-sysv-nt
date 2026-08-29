# Proposal 0004 — what `linux` claims, and where the claim stops

Status: raised; DR-0001 stands and is not reopened
Date: 2026-08-29
Raised by: the operator, against `doc/target-definition.md`

## The problem

Three tracked documents describe the target triple as `cpu-vendor-os-abi` and
call the `linux` in it a lie. Both halves are wrong. The second half is worse
than wrong, because it invites a reopen that would cost this project the entire
dividend WP-12 collected on 2026-08-29.

`doc/target-definition.md` puts it plainest, in the `uname` section: "the os
field of the triple already tells the same lie, deliberately".
`doc/elf-technical-breakdown.md` and `doc/milestones.md` carry the same framing
in nearly the same words, that only `os` and `abi` are load-bearing and the
vendor slot is where a truthful name costs least. A reader who takes that at
face value concludes the triple is dishonest in a field that matters, goes
looking for an honest replacement, and discovers that every replacement is
either refused outright or ruinous. This proposal corrects the framing, states
what `linux` does claim, and names the one axis where the claim runs past what
this project delivers.

## What the fields are actually called

Measured on 2026-08-29 against the `config.sub` in `toolchain/config/`'s pin,
upstream timestamp 2026-05-17.

A four-field triple is `cpu-vendor-kernel-os`, and `config.sub` says so in its
own variable names. It sets `kernel=linux` and `os=gnu` for a triple like
`x86_64-pc-linux-gnu`, and it validates the two together at the end of the
script under `case $kernel-$os-$obj`. No field is named `abi`. What this
project's documents have been calling the abi field is the C library, which is
why `linux-musl`, `linux-uclibc`, `linux-dietlibc` and `linux-android` are all
the same kernel under a different libc, and why `kfreebsd-gnu` is that trade
run the other way.

So `gnu` is not a hedge, a placeholder, or a field we tolerate because
configure reads it. It names glibc, and glibc is what this project ships: ELF
objects, versioned symbols, `libc.so.6`, `ld-linux-x86-64.so.2`, the
`/usr/lib64` layout that `doc/target-definition.md` fixes. That field is true
without qualification, and nothing in `doc/` currently says so.

## What `linux` claims

The kernel field claims the Linux kernel ABI: system call numbers, `futex`,
`clone`, the vDSO, `/proc`, the auxv a process is entered with. It also claims,
and this is the load-bearing one, that a `syscall` instruction reaches a
kernel.

Everything on that list except the last item, this project supplies. Supplying
them is the project. `doc/elf-technical-breakdown.md`'s second bridge refuses
to translate, rebuilds each package against `elfsysv1.dll`, and arrives at the
sentence that bounds the claim: there is no `syscall` instruction left to
catch. An object reaching the kernel through a raw `syscall` rather than
through a call into our runtime sits outside the contract, and no field of any
triple expresses that restriction, because no target before this one has
needed to express it.

The correct statement, then, is not that the os field lies. It is that the
kernel field over-claims along exactly one axis, that the axis has a name, and
that the name is raw syscall dispatch. Everything else `linux` implies is
delivered or is on the plan to be delivered.

## Why this is not a reopen

Three spellings, fed to the pinned `config.sub` on 2026-08-29:

    x86_64-elfsysvnt-linux-gnu        echoed unchanged, rc 0
    x86_64-elfsysvnt-linux-elfsysvnt  refused, rc 1
    x86_64-pc-elfsysvnt               accepted, rc 0

The second is refused, and the refusal is the kind outcome. `config.sub`
allowlists ten libc values against the `linux` kernel and rejects the rest with
`Invalid configuration: Kernel 'linux' not known to work with OS ''`. Even had
it passed, replacing the libc field asserts something false in the opposite
direction, since the libc genuinely is glibc, and it would miss every
`*-*-linux-gnu*` arm in libtool, in glibc's own configure, and in ordinary
package configury.

The third reconfirms DR-0001's trapdoor against current upstream rather than
against the 2021 file that record measured. An honest kernel field is accepted
rather than refused, and `config.gcc` then reads `x86_64-*-elf*` as bare metal
and hands the triple a target definition with no operating system beneath it.
Past that acceptance sits the real bill, which is a port: `config.gcc` and a
target macro header, a `bfd` vector and an `ld` emulation that no longer
inherit from the Linux ones, a `sysdeps` tree in glibc for a kernel that is
neither Linux nor Hurd, an upstreaming into GNU config, tuples in CMake and
LLVM and Rust, and `case $host_os in linux*)` in most of the 2893. WP-12
measures what would be thrown away: binutils built for
`x86_64-elfsysvnt-linux-gnu` with no port at all, passing ten acceptance
claims. Midipix is the worked precedent for the other road, at
`x86_64-nt64-midipix` over musl, with a permanently forked toolchain and no
vendor RPMs at the end of it. Recalled, not measured.

Spike 5's one package in 2893 prices the vendor field. It says nothing about
this one, and the two numbers are not in the same decade.

## Where the honest name goes

`doc/target-definition.md` already answered this and did not notice it had.
The name lives in the vendor field, in `uname -r`, and in
`.note.elfsysvnt.abi`. The triple names the ABI contract a compiler must
target; the notes name the implementation that satisfies it. That separation
is the design, and a change to any field would collapse it. This proposal
argues only that the separation be stated, since a document that calls one
half a lie is not stating it.

## What changes

Nothing built, nothing shipped. The triple, the `EI_OSABI` rule, the ABI-tag
note, the loader SONAME and the `uname` strings are untouched, and no package
rebuilds. What changes is what `doc/` says about them:

- `doc/target-definition.md` gains a section fixing the limit of the `linux`
  claim, and its `uname` section stops calling the kernel field a lie.
- `doc/elf-technical-breakdown.md` and `doc/milestones.md` name the fields
  `kernel` and `os`, cite the measurement above, and keep every conclusion
  they already reached.
- `doc/ROADMAP.md`'s assumed-path row, `doc/IMPLEMENTATION-PLAN.md`'s WP-10,
  and `AGENTS.md`'s reservation point at the record this proposal produces.

The bound also gives `doc/proposals/0003-vendor-binary-tls-rewriting.md` a
stated home. Vendor binaries carry raw syscalls nobody here compiled, so the
one axis where `linux` over-claims is precisely the axis on which prebuilt
el8 objects sit, and the project should admit that in one place rather than
leave it implied across three documents.

## What it costs to be wrong

If the bound turns out to be unenforceable, a package that will not build
without inline syscalls, say, or a vendor binary whose syscalls matter more
than its TLS does, then the bound is what moves. Widening it means some
measure of the translation bridge `doc/elf-technical-breakdown.md` rejected,
at whatever scale the offending set demands, and that is a new proposal
against the loader rather than against the triple. DR-0001 survives either
way, which is the point of writing the limit down where it can be revised
alone.
