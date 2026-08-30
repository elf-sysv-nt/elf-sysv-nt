# DR-0027 — one classifier for the exec branch, and a four-hop interpreter limit

Status: accepted
Date: 2026-08-30
Deciding: the loader track, WP-41

## Context

Linux decides what an executable is inside the kernel, in a table it calls
binfmt: the first bytes of the file are read once and offered to each format in
turn until one claims them. There is no kernel here, so the table moves into the
spawn path of the library Cygwin already funnels every host call through, and
moving it raises two questions that the kernel answered long ago and that this
tree has to answer for itself.

The first is order. Cygwin's spawn path already recognizes `#!` and already
hands everything else to `CreateProcess`. Adding ELF to that means deciding
where the new test goes relative to the two that are there, and the plan for
this package says the ordering has to be written down rather than inherited by
accident.

The second is depth. A `#!` interpreter may itself be a script, and that script
may name another. Following the chain has to stop somewhere, and where it stops
is visible to programs: a chain one hop shorter than the limit works and one hop
longer does not, so the number is part of the interface rather than an
implementation detail.

## What was decided

There is one classifier, `binfmt_classify`, over one read of the leading bytes,
and it produces one of four verdicts: ELF, a `#!` script, an image the host
still owns, or nothing recognized. The order it tries them in is ELF, then
`#!`, then the host's own `MZ`, and the first match wins with the rest not
consulted. The spawn path calls that classifier instead of testing for ELF in
front of the `#!` test it already had.

The chain follows at most four interpreter hops. A fifth is refused, and the
same limit is what refuses a cycle: a script whose interpreter is itself simply
spends its four hops and is turned away, with no separate cycle detection.

At each hop the argument vector is rebuilt the way the kernel rebuilds it. The
leading element is dropped, and the interpreter, its single optional argument if
the line carried one, and the path of the file that named the interpreter are
pushed in front of what remains. The interpreter's argument is the whole rest of
the line rather than a further split, and trailing blanks are stripped from it.
A `#!` line that does not end within the first 256 bytes is refused rather than
truncated.

The vector is rebuilt whether or not the chain ends in ELF. A script whose
interpreter turns out to be an ordinary host program is the host's to run, and
it is handed back with the rebuilt vector and the resolved file rather than with
the arguments the caller first supplied.

## Why, and what was given up

The first byte alone separates `0x7f`, `#` and `M`, so no file can satisfy two
of these tests and the order is, today, unobservable. That is the argument for
not bothering, and it is the argument for bothering: the tests do not conflict
now, and they would begin to conflict the first time either grew a second
condition, at which point the conflict would be discovered by a program running
the wrong thing. One classifier that cannot disagree with itself costs a
function call and removes the class.

The 256-byte head and the four hops are the kernel's numbers rather than ours.
A script that works on Linux has to work here, and a longer head or a deeper
chain would accept scripts that Linux refuses, which is a compatibility bug
pointing the pleasant direction and still a bug — a build that succeeded here
and failed on the platform being imitated is worse than one that fails in both
places. The four is taken from the limit Linux has long carried under the name
`BINPRM_MAX_RECURSION`; it has not been confirmed against a kernel source at a
named revision, and that is recorded below rather than asserted in the code.

Refusing an over-long `#!` line rather than truncating it is a deliberate
difference from the oldest kernels, which truncated at the buffer. A truncated
interpreter path names a different program, and silently running a different
program is a worse failure than declining to run any. Current Linux refuses too.

Handing back the rebuilt vector rather than the original one means the host path
and the ELF path agree about what a chain means. The alternative — returning
"not mine" and letting Cygwin's `execve` walk the `#!` chain again with its own
code — has two walks that can disagree, and the disagreement would show up as a
script that behaves differently depending on what its interpreter turned out to
be.

## Not verified

The four-hop limit is matched to Linux from memory of `BINPRM_MAX_RECURSION`
rather than from `git show` against a kernel at a known tag. The number is
cheap to change and the shape is not, but a reader should treat the claim that
it equals Linux's as unconfirmed until someone checks it against a source.

Whether Cygwin's spawn path can be patched to call this classifier without
disturbing the `#!` handling it already has is untested here: winsup is not in
this tree, and the branch is certified through `elfsysv-exec`, a front end that
calls it exactly as the spawn path will. The obligations that come with exec —
descriptor inheritance and close-on-exec, the working directory, signal
disposition, the environment — are discharged by that spawn path for the PE case
already and are not re-implemented here, so nothing in this package has tested
that they survive the ELF branch.
