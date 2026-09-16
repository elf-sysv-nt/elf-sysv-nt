# DR-0111 — the gate address reaches a program through a symbol libc exports, because the alternatives cannot be made correct

Status: provisional
Date: 2026-09-16
Deciding: the build worker, on spike 55; the operator ratifies
Proposal: none; taken when `build-glibc` stopped in `nscd` and the ladder
discriminated at tier 1 rather than reaching the operator
Amends: doc/design/target-definition.md § What the `linux` claim means

## What was decided

`ENTER_KERNEL`'s third arm — the one every object of a program takes, and every
object of the static libc with it — reaches the gate through `_dl_sysinfo`, and
libc defines and exports that symbol rather than leaving it to
`elf/dl-support.c` and the static link alone. A dynamically linked program
resolves it against `libc.so`; a static one resolves it against `libc.a`; the
text in the arm is identical either way and no condition selects between them.

This is not a preference between mechanisms. Two other shapes were available
and both are eliminated on correctness.

## Why the other two are gone

**A third arm conditioned on `PIC`.** The repair that reads as obvious: give
the arm the `_rtld_global_ro` indirection the `SHARED` arm has, behind a
condition that means "static link". There is no such condition. The object that
fails to link and an object destined for the static libc are compiled with
identical flags, neither `SHARED` nor `PIC`, and across the whole build
`SHARED` never appears without `PIC` — 3,568 compile lines carry both, 168
carry `PIC` alone, none carries `SHARED` alone, and 3,061 carry neither. glibc
does not know, while compiling `nscd_setup_thread.c`, whether the program that
object joins will be linked against `libc.so` or against `libc.a`; the
makefiles do not tell it and no flag carries the information.

**Reading the thread pointer**, which is what upstream i386 does and what makes
one text serve both links. The port declined it, and the reason in
`sysdeps/unix/sysv/linux/x86_64/sysdep.h` is that a static program allocates
its TLS with `brk` before it has a thread pointer at all. That reason was
asserted and is now measured: `csu/libc-tls.c` calls `__sbrk` at line 151 and
sets the thread pointer with `TLS_INIT_TP` at line 192, and `brk` reaches the
kernel through `INLINE_SYSCALL` from an object in the third arm. A static
program's first gate call would read a thread pointer that does not exist. The
carve-out this would need is the condition the paragraph above shows cannot be
written.

One candidate remains, so the ladder stopped at tier 1 and nothing below it was
consulted. Recording that is the point of this section: had either elimination
failed, this would have been a preference and the operator's, which is what the
working note behind it assumed.

## Consequences

`_dl_sysinfo` joins the library's exported surface as `GLIBC_PRIVATE`, which is
a wire contract and the real cost of this decision. Two obligations come with
it. The word libc holds must be the word `ld.so` holds, initialised from
`AT_SYSINFO` on both paths, or a program and its loader will disagree about
where the kernel is. And a version-script entry is part of the port's patch
rather than a build-time accident, so that the symbol's presence is something
the tree states rather than something a reader discovers with `readelf`.

`toolchain/glibc/build-glibc` gets past `nscd`, which is what unblocks the
port's own bar, WP-15's run-time half behind it, and the first dynamic program.

## What it does not decide

How the two words are kept in step. Initialising both from `AT_SYSINFO` is the
obvious answer and there may be an ordering subtlety in a static program, whose
auxv is read by its own startup rather than by a loader; that is
implementation and it is measured by the port building and its bar passing.

Whether `_dl_sysinfo` is the right name. It is the name the third arm already
uses and the name upstream uses for the same word, so changing it would be a
second decision with no benefit named here.

## Not verified

That the surviving candidate works. Nothing has been built with it. The two
eliminations are measured and the survivor is inferred from them, which is a
weaker claim than a passing build and is the reason this record is provisional
rather than accepted.

That `__sbrk` is the earliest gate call on the static path. An earlier one
would not change the finding, since line 151 already precedes line 192, but the
claim made here is about that call and not about a complete ordering of static
startup.

That the dynamic path has the same constraint. A dynamically linked program has
its thread pointer set by `ld.so` before it runs, so the thread-pointer shape
might have served it; the arm serves both links and the static case decides it,
which means the dynamic case was never measured.
