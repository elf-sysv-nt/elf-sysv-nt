# Requirements

What the platform must do, and how anyone will know it does.

This is short on purpose. Almost everything this project has to satisfy was
specified elsewhere, by people who own the specification: the gABI for the
object format, the psABI for the calling convention and the register file,
glibc's own documentation for symbol versioning, and el8 itself for the
behaviour a package was built expecting. Restating any of that here would give
a reader a second copy, worse than the first and free to drift from it. What
belongs here is the part nobody else can write down: the boundary of this
project's ambition, the classes of conformance inside it, and the number that
says when the job is finished.

`doc/design/Architecture.md` says what the system is.
`doc/design/Verification-Plan.md` says what counts as proof for each class
below. This document says what must be true.

## Scope

A userland kernel that presents ELF and the System V AMD64 ABI upward and
Windows NT downward, so that el8's userland builds against it with its object
format, its symbol versioning, and its loader semantics intact. Three
obligations follow, and they are the whole of the scope:

An el8 source package rebuilds against this platform with no change to the
package. Its configure script finds what it looks for, its compiler emits ELF
for `x86_64-elfsysvnt-linux-gnu`, and its link line resolves against a
`libc.so.6` that answers by glibc's names at glibc's version nodes.

A rebuilt package runs. The loader maps it, relocates it, resolves its symbols
against the version rules glibc's own loader would apply, runs its
initializers in the order the specification requires, and hands control to its
entry point. Nothing in that path asks the Windows loader to understand ELF.

A running package observes el8's semantics. The errno it reads, the signal it
catches, the struct it copies out of a system call and the constant it passed
in are all the values a package built for el8 was compiled against, whatever
the runtime beneath the face happens to use.

## Non-goals

Not a Linux kernel. The kernel ABI is satisfied by rebuilding against
`elfsysv1.dll`, not by dispatching system calls, so an object that reaches the
kernel through a raw `syscall` instruction is outside the contract the triple
advertises. That bound is a requirement here and not merely DR-0005's
argument: a package which insists on inline system calls is out of scope until
a record widens the bound, and the widening lands in the loader.

Not a Cygwin replacement. The floor is Cygwin re-faced. Where Cygwin's
behaviour is the platform's behaviour, that is the answer and not a placeholder
for a better one; a body cannot compose its way above the floor it stands on.

Not a binary compatibility layer. Nothing here promises to run a binary built
on a real Linux. Vendor binaries are read as oracles and as certification
input; running them unmodified is a different project and is not this one.

Not portable beyond the pinned pair. One target triple, one runtime version,
one el8. Anything else is a later question and no requirement here anticipates
it.

## Conformance classes

Every obligation above falls into one of three classes, and the class decides
what proof is owed. `doc/design/Verification-Plan.md` states the proof; this
states the class.

Class A, bit-exact. Anything a program can observe as bytes or as a number it
was compiled against: struct layouts crossing the face, errno values, signal
numbers, flag and mode constants, the ELF image's own shape, and the symbol
version nodes the linker records. These match el8's glibc 2.28 exactly. A
difference here is a defect regardless of whether any package has noticed it,
because the package that notices was compiled years ago and cannot be asked.

Class B, behaviourally equivalent. Anything a program observes only through
its effects: symbol resolution order, initializer order, the contents of the
auxiliary vector, TLS layout as seen through `__tls_get_addr` rather than as
laid out in memory, the loader's search path. These match what a real glibc
does, checked against one rather than against this project's reading of the
specification. Representation is free; behaviour is not.

Class C, divergence with a recorded delta. Where the floor cannot reach class
A or B and the gap is judged acceptable, the gap is written down before it
ships. A recorded delta names what differs, where a caller can observe it, and
why the alternative was refused; DR-0053's two-byte `wchar_t` and DR-0054's
termios layout are the shape of one. An unrecorded divergence is not class C.
It is the failure the class exists to make impossible.

Every symbol on the face belongs to exactly one class, and the classification
tables under `veneer/libc/` are where that assignment lives.

## Acceptance

Acceptance is a count, not a judgement.

Of the 2893 packages in the el8 set, at least <N, the operator's> rebuild
from vendor source against this platform, run, and pass their own test suites,
with no change to the package and no substitution left open against them in
`doc/design/substitutions.md`.

Three things about that sentence carry weight. It counts packages rather than
symbols, because a symbol surface that links proves nothing about a program
that runs: an export face can be green while every body behind it is a bare
`ret`, which is a state this tree is capable of reaching and has reached. It
requires the package's own test suite
rather than a smoke run, because the package's authors wrote a better
acceptance test for their package than this project will. And it requires the
package to be unmodified, because a patched package measures the patch.

The count is reported by the acceptance harness, `acceptance/accept.sh` in
embryo today over one pinned leaf and WP-T4 at full width, and a demand-side
census over the vendor binaries' undefined symbols is what says in advance
which packages the classification can currently reach. That census is unrun,
which is design-gaps finding F4; running it is what turns this criterion from
a target into a forecast.

The number is set after that census rather than before it, decided on
2026-09-03. Setting it first would make it an appetite; setting it second makes
it a judgement against a measured reach. The census is therefore a gate on this
section closing, not merely an input to it, and it is one pass over the vendor
binaries' undefined symbols against the classification — cheap in the way spike
5 was cheap.

One thing the census must count, because it is currently uncounted and the
bound above does not cover it: how many packages call `syscall` the libc
function directly. It is a public glibc export, its disposition here is a
bucket-4 stub, and a rebuilt package reaching it has used no raw instruction,
so DR-0005's bound does not reach the case. Whichever way the count falls, the
answer is a record widening or restating that bound against a number rather
than against an impression.

## Not verified

The number itself. It is the operator's to set and is a marked blank until
they set it, so nothing in this document is yet a bar anything can be measured
against.

That 2893 is the right denominator. It is the el8 package count this project
has used throughout, and no pass over the vendor manifests has confirmed it
against the Rocky 8.10 source set DR-0002 pins.

That the three classes partition the face. They were derived by reading what
the records already assert, not by walking `veneer/libc/`'s tables and
assigning every row; a symbol that fits none of the three is a finding about
this document.

That a package passing its own test suite means the platform is correct for
that package. It means the package's authors' tests pass, which is a weaker
claim, and the differential work in `doc/design/Verification-Plan.md` exists
because of the gap between them.
