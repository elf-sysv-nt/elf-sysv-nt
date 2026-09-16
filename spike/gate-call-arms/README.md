# Which arm of the gate call each glibc object takes

`toolchain/glibc/build-glibc` stops in `nscd` with `undefined reference to
_dl_sysinfo`. The port's `ENTER_KERNEL` has three arms and the third, whose
comment calls it "a static link", is the one `nscd` takes.

Two questions, and between them they settle the repair.

Does any compile-time flag tell a dynamically linked program's object apart
from a static link's? No. The object that fails and an object destined for the
static libc are compiled with identical flags — neither `SHARED` nor `PIC` —
and across the whole build `SHARED` never appears without `PIC`, so no
condition written over the two separates them.

Does a static program call the kernel before it has a thread pointer? Yes.
`csu/libc-tls.c` calls `__sbrk` at line 151 to place the program's own TLS and
sets the thread pointer with `TLS_INIT_TP` at line 192, and `brk` reaches the
kernel through `INLINE_SYSCALL` from an object in the third arm.

`results-2026-09-16.txt` is the transcript.

## Why it matters

It eliminates a candidate rather than choosing one, which is the useful kind of
measurement here.

The repair that reads as obvious — give the third arm the `_rtld_global_ro`
indirection the `SHARED` arm has, behind a condition that actually means static
— cannot be written, because there is no such condition. glibc does not know,
while compiling `nscd_setup_thread.c`, whether the program that object joins
will be linked against `libc.so` or against `libc.a`; the makefiles do not tell
it, and the flags carry no trace of it. Anything conditioned on `PIC` puts the
failing object in the same arm it is already in.

So whatever the third arm does, it has to work for a dynamic program and a
static one alike. Three shapes were available and two of them are gone.

**Read the thread pointer**, which is what upstream i386 does: a program's
objects reach the entry through the TCB rather than through a symbol, so the
same text serves either link. **Eliminated.** The port declined this and gave a
reason it did not measure — that a static program allocates its TLS with `brk`
before it has a thread pointer at all — and the reason is correct. The call is
at line 151 and the thread pointer arrives at line 192, so a static program's
first gate call would read a thread pointer that does not exist yet. Carving
the static case out needs the condition the first question shows does not
exist.

**Condition the arm on `PIC`.** **Eliminated** by the first question: the
failing object and a static-libc object carry the same flags.

**Export the symbol.** Define `_dl_sysinfo` in libc proper as well as in
`elf/dl-support.c`, initialise it from `AT_SYSINFO` at startup, and export it
`GLIBC_PRIVATE`, so `call *_dl_sysinfo(%rip)` resolves against `libc.so` for a
dynamic program and against `libc.a` for a static one, with no condition
required and the text identical in both. It adds a symbol to the library's
exported surface, which is a wire contract, and it needs the word libc holds to
agree with the one `ld.so` holds. **It is what is left**, and the ladder
reaches it at tier 1 without needing a preference: the other two cannot be made
correct.

**Gates.** `toolchain/glibc/patches/0001-x86_64-reach-the-kernel-through-the-gate-and-the-thr.patch`
and `toolchain/glibc/build-glibc`, which is the build that stops.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Needs the make log a glibc build leaves at
`$ELFSYSVNT_EL8/glibc/build/glibc-make.log`; seconds, no build.

## Method, in two parts

For one object of each kind — the failing `nscd` object, a second `nscd`
object, an `ld.so` object, two libc objects and one built for the static
libc — the first compile line for that basename is pulled out of the log and
its `-DSHARED`, `-DPIC` and `-DMODULE_NAME` are read off. glibc compiles most
sources more than once with different flags, and the first pass is the plain
`.o` one; the other passes are counted in the tallies rather than listed.

The tallies are what make the finding general rather than a claim about six
files: every compile line in the build is classified by which of the two flags
it carries.

The second part reads source rather than a log: the line at which
`csu/libc-tls.c` first calls `__sbrk`, the line at which it sets the thread
pointer, and whether `brk` on this target reaches the kernel through
`INLINE_SYSCALL`. Three facts, and the ordering of the first two is the whole
of it. This is a reading of the source and not an execution of it, which is
the weaker kind of evidence and is why it is stated as line numbers a reader
can check rather than as a verdict they have to trust.

## What this does not reach

That the surviving candidate works. Nothing has been built with it; this
measures why the other two cannot be, which narrows the field without
demonstrating the winner. A build is the only thing that will.

The dynamic side of the startup question. What is measured is a static
program calling the kernel before its thread pointer exists. A dynamically
linked program's thread pointer is set by `ld.so` before control reaches the
program, so the same objection does not obviously apply there — but the arm is
one arm and has to serve both, so the static case decides it either way.

That `__sbrk` is the first such call rather than merely an early one. An
earlier gate call on the static path would strengthen the finding and not
change it; a later one would not weaken it, since line 151 already precedes
line 192.

One glibc, one configuration. The flags are el8's 2.28 as this project
configures it, and a different `--enable`/`--disable` set could carry different
ones — though not, on this evidence, ones that would separate the two cases,
since the information is not available to the compiler at all.
