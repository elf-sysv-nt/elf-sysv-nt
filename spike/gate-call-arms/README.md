# Which arm of the gate call each glibc object takes

`toolchain/glibc/build-glibc` stops in `nscd` with `undefined reference to
_dl_sysinfo`. The port's `ENTER_KERNEL` has three arms and the third, whose
comment calls it "a static link", is the one `nscd` takes.

Does any compile-time flag tell a dynamically linked program's object apart
from a static link's?

No. The object that fails and an object destined for the static libc are
compiled with identical flags — neither `SHARED` nor `PIC` — and across the
whole build `SHARED` never appears without `PIC`, so no condition written over
the two separates them. `results-2026-09-16.txt` is the transcript;
`finding=program-and-static-objects-are-indistinguishable-at-compile-time`.

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
static one alike. That leaves two shapes, and the choice between them is the
operator's:

**Export the symbol.** Define `_dl_sysinfo` in libc proper as well as in
`elf/dl-support.c`, initialise it from `AT_SYSINFO` at startup, and export it
`GLIBC_PRIVATE`, so `call *_dl_sysinfo(%rip)` resolves against `libc.so` for a
dynamic program and against `libc.a` for a static one. It adds a symbol to the
library's exported surface, which is a wire contract and a one-way door, and it
needs the word libc holds to agree with the one `ld.so` holds.

**Read the thread pointer.** What upstream i386 does: a program's objects reach
the vsyscall entry through the TCB rather than through a symbol, so the same
text works either way. The port declined this deliberately and its reason is in
the header — `ld.so` maps and protects segments before it has set the thread
pointer up, and a static program allocates its TLS with `brk` before it has one
at all. That reason is about `ld.so` and about early static startup, and both
are the `IS_IN (rtld)` arm and the earliest static path rather than the arm in
question. Whether any gate call on those paths happens before the thread
pointer exists is a measurable question this spike does not answer.

**Gates.** `toolchain/glibc/patches/0001-x86_64-reach-the-kernel-through-the-gate-and-the-thr.patch`
and `toolchain/glibc/build-glibc`, which is the build that stops.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Needs the make log a glibc build leaves at
`$ELFSYSVNT_EL8/glibc/build/glibc-make.log`; seconds, no build.

## Method

For one object of each kind — the failing `nscd` object, a second `nscd`
object, an `ld.so` object, two libc objects and one built for the static
libc — the first compile line for that basename is pulled out of the log and
its `-DSHARED`, `-DPIC` and `-DMODULE_NAME` are read off. glibc compiles most
sources more than once with different flags, and the first pass is the plain
`.o` one; the other passes are counted in the tallies rather than listed.

The tallies are what make the finding general rather than a claim about six
files: every compile line in the build is classified by which of the two flags
it carries.

## What this does not reach

Whether either surviving candidate works. Neither has been built; this measures
why the third does not exist.

The early-startup question. The TCB candidate turns on whether a gate call
happens before the thread pointer is set, on the `ld.so` path and on the static
one. Answering it means reading those paths or instrumenting them, and this
spike reads compile flags.

One glibc, one configuration. The flags are el8's 2.28 as this project
configures it, and a different `--enable`/`--disable` set could carry different
ones — though not, on this evidence, ones that would separate the two cases,
since the information is not available to the compiler at all.
