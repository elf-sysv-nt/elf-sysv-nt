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

The count is reported by the acceptance harness, `acceptance/accept.sh`, over
the packages pinned in `acceptance/packages.tsv`; the wider comparison against
what Red Hat shipped belongs to `rhelcyg-8.10` and DR-0082 retired the work
package that claimed it here.

The number is set after the demand-side census rather than before it. Setting
it first would make it an appetite; setting it second makes it a judgement
against a measured reach. That census has since run, and `spike/demand-census/`
holds both halves of what it says.

The reach is narrower than the count of packages suggests, and the reason
matters more than the number. Of the 3046 Rocky 8.10 packages that link a
glibc soname, 1898 need an interface the floor beneath does not have, 625 need
only glibc's own internals, and 516 are reachable against the classification as
it stands. The first class is the bound on any acceptance count: no amount of
veneer work reaches it, because the capability is absent rather than unexported.
The second is what a glibc port converts. At 62.3% the first class sits in the
band the census reserves for a program-level review, and that reading is owed
and is not taken here.

The whole-set share, 52.1%, is the weaker statement and should not be the one
quoted: a third of the 4855 packages scanned carry no 64-bit ELF at all and
were never participants in the question.

One thing changed under the number since it was written, and it changes what
the number does. DR-0079 derives the claimed surface from what the acceptance
set imports, so the set this count runs over is also the set that decides which
names the veneer exports at all. Raising the count widens the surface and the
bodies owed behind it. The count is therefore a scope control as much as a bar,
and it should be read as one.

Which splits the count in two, and they are not summed. A package that adds no
name to the claimed surface is free: its bodies were written for another
package's demands and are already claimed, so passing it tests the surface
rather than extending it, and failing it is a defect in something believed
finished. A package that adds names buys breadth and pays for it in bodies
owed. Reported as one number the cheap counter carries it, and the count climbs
while the platform does nothing new; reported separately, the incentive
disappears, because the cheap counter says it is cheap. `spike/demand-census/`
ranks every package by which it is.

Neither counter measures how thoroughly a passing package exercises what it
links. The acceptance harness reports that separately, over the claimed surface
rather than over the package set, and it is the number that stays honest when
the other two are both small.

One thing the census must count, because it is currently uncounted and the
bound above does not cover it: how many packages call `syscall` the libc
function directly. Under a kernel at the syscall boundary the question changes
shape rather than going away -- the instruction is the interface under H and is
never reached under N -- so what the count now bounds is how much of the el8 set
needs the gate-calling rebuild rather than the shipped binary. Whichever way it
falls, the answer is a record written against a number rather than against an
impression.

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
