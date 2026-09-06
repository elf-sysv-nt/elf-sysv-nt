# DR-0105 — the conformance classes are defined at the syscall boundary

Status: accepted
Date: 2026-09-06
Deciding: the run, under the operator's grant of 2026-09-06, on proposal 0013 § 2
Proposal: 0013
Amends: doc/design/Requirements.md § Conformance classes

## What was decided

`Requirements.md`'s three classes keep their names and change their
subject. Class A, bit-exact, is the syscall ABI's numbers, constants,
struct layouts, `uname` and the auxiliary vector's shape, checked against
el8's 4.18 kernel headers and generated from them where a table is large
enough to be typed wrong. Class B, behaviourally equivalent, is everything
a criterion measures by differential against the Rocky 8 oracle. Class C, a
recorded divergence, is what a phase's document records where the host
refuses Linux's semantics and the gap is accepted. Every syscall the table
implements belongs to exactly one class, and the table under `core/` is
where the assignment lives.

## Why

The classes were defined over "the face", the veneer's exported symbol
surface, and the tables under `veneer/libc/` that assigned every symbol;
neither exists. The classes themselves are sound for a kernel: the
criteria already sort into tables checked against headers, behaviour
checked against an oracle, and divergences written down, and the
verification plan's proof rule per class is what lets a certification be
read against a bar that existed before it ran. Tier 2 of the ladder: the
alternative, one undifferentiated bar, loses the distinction between a
constant that must match bit for bit and a behaviour that must match an
oracle, which is the distinction every criterion is written to.

## Consequences

`Requirements.md` § Conformance classes and `Verification-Plan.md` § The
bar per conformance class state the classes and cite this record. The
generation of class A tables from headers is owed and listed as not
verified until it exists.
