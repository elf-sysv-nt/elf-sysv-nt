# Requirements

What the platform must do, and how anyone will know it does.

This is short on purpose. Almost everything this project has to satisfy was
specified elsewhere, by people who own the specification: the Linux kernel
ABI for what a system call takes and returns, the gABI for the object
format, the psABI for the calling convention and the register file, and el8
itself for the behaviour a package was built expecting. Restating any of
that here would give a reader a second copy, worse than the first and free
to drift from it. What belongs here is the part nobody else can write down:
the boundary of this project's ambition, the classes of conformance inside
it, and the number that says when the job is finished.

`doc/design/Architecture.md` says what the system is.
`doc/design/Verification-Plan.md` says what counts as proof for each class
below. This document says what must be true.

## Scope

A Linux kernel personality for Windows NT: one core presenting the x86-64
Linux kernel ABI as el8's 4.18 kernel defines it, over two substrates that
differ in how user code is run and nowhere else. Under substrate N, the
native one, el8's userland is rebuilt from source against a syscall gate,
because NT cannot trap the `syscall` instruction; under substrate H, the
hypervisor one, el8's userland is installed as shipped and the instruction
is the interface. Three obligations follow, and they are the whole of the
scope:

An el8 source package rebuilds against substrate N with no change to the
package. Its configure script finds what it looks for, its compiler emits
ELF for `x86_64-elfsysvnt-linux-gnu`, and its link line resolves against
the glibc that the same rebuild produced.

A package runs. Under either substrate the kernel maps its image, builds
the initial stack and auxiliary vector a Linux program expects, and hands
control to glibc's own `ld.so` for everything dynamic; nothing in that path
asks the Windows loader to understand ELF.

A running package observes el8's kernel semantics. The errno it reads, the
signal it catches, the struct it copies out of a system call and the
constant it passed in are Linux's by construction (0011 § 1), and the
behaviour behind them is measured against a Rocky 8 oracle rather than
assumed.

## Non-goals

Not a security boundary. Nothing here confines a Linux process to less
than the Windows user can do; a process that is root inside the tree is
still the Windows user outside it. Sandboxing is a different design with a
different threat model.

Not an init system. The supervisor owns the process table and reaps
orphans; it is not systemd, runs no units, and `systemctl` reports that
the system was not booted with systemd.

Not a container host. Network namespaces, cgroups and the stack above them
are outside; mount, pid and user namespaces are kept because the design
gives them nearly free.

Not, under substrate N, a home for a binary that carries a raw `syscall`
instruction or a `%fs`-relative load. The post-link check refuses such an
image at build time, and what the el8 set holds of them is counted rather
than guessed (spike 51). Under substrate H the same binaries run as
shipped; that is the reason there are two substrates.

The bound the old text stated in this place, "not a Linux kernel", is
inverted, and a reader who remembers it should read this section again: a
kernel at the syscall boundary is what this is.

## Conformance classes

Every obligation above falls into one of three classes, and the class
decides what proof is owed. `doc/design/Verification-Plan.md` states the
proof; this states the class.

Class A, bit-exact. Anything a program can observe as bytes or as a number
it was compiled against: the syscall numbers, the constants and flag
values, the struct layouts crossing the boundary, `uname`, the shape of the
auxiliary vector, the ELF image's own shape. These match el8's 4.18 kernel
headers exactly. A difference here is a defect regardless of whether any
package has noticed it, because the package that notices was compiled
years ago and cannot be asked.

Class B, behaviourally equivalent. Anything a program observes only
through its effects: what a sequence of file operations returns, how a
signal is delivered, what `fork` copies, how a pipe blocks. These match
what el8's kernel does, checked against one rather than against this
project's reading of the manual. Representation is free; behaviour is not.

Class C, divergence with a recorded delta. Where the host cannot reach
class A or B and the gap is judged acceptable, the gap is written down
before it ships: what differs, where a caller can observe it, and why the
alternative was refused. NTFS keeping timestamps at 100 ns, a truncate
under a live mapping refused, a directory that cannot be renamed while a
handle is open beneath it (`Core-Phase2.md`, spike 39) are the shape of
one. An unrecorded divergence is not class C. It is the failure the class
exists to make impossible.

Every syscall the table implements belongs to exactly one class, and the
table under `core/` is where that assignment lives.

## Acceptance

Acceptance is a count, not a judgement, and it is counted per substrate.

Under substrate N: of the packages in the el8 set, at least <N, the
operator's> rebuild from vendor source against this platform, run, and
pass their own test suites, with no change to the package and no
substitution left open against them in `doc/design/substitutions.md`.

Under substrate H: of the same set, at least <M, the operator's> install
from Rocky Linux 8's own RPMs, run, and pass their own test suites,
unmodified. The sentence is N's with the rebuild removed, which is what
H is for.

Three things about those sentences carry weight. They count packages
rather than syscalls, because a syscall table that answers proves nothing
about a program that runs. They require the package's own test suite
rather than a smoke run, because the package's authors wrote a better
acceptance test for their package than this project will. And they require
the package to be unmodified, because a patched package measures the
patch.

The two counts are reported side by side and are not summed: a package
that passes under H says nothing about the rebuild, and one that passes
under N says nothing about the shipped binary. The kernel's own bar sits
beneath both: the criteria of `doc/design/Verification-Plan.md` § The
kernel's criteria are what the kernel must pass before a package count
means anything.

The numbers are set after the reach is measured rather than before.
Setting them first would make them appetites; setting them second makes
them judgements against a measured reach. The measurement under N is
spike 51's census: the packages whose shipped text carries a raw `syscall`
outside glibc, or a Go runtime, are the ones N cannot take as shipped and
the post-link check would refuse after a rebuild, and that list bounds
N's count from above. Under H the bound is the kernel's syscall table and
nothing about the packages.

## Not verified

The numbers themselves. They are the operator's to set and are marked
blanks until they are set, so nothing in this document is yet a bar
anything can be measured against.

That the el8 set is the right denominator. It is the Rocky 8.10 x86_64
package set spike 51 enumerates, 3780 packages after the names that carry
no runnable text are dropped; no pass over the vendor manifests has
confirmed that against the source set DR-0002 pins.

That the three classes partition the syscall table. They were derived by
reading what the criteria measure, not by walking every implemented
number and assigning it; a syscall that fits none of the three is a
finding about this document.

That a package passing its own test suite means the platform is correct
for that package. It means the package's authors' tests pass, which is a
weaker claim, and the differential work in
`doc/design/Verification-Plan.md` exists because of the gap between them.

Settled by: DR-0104, DR-0105.
