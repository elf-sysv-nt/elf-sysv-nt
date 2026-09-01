# What a stub means

The classification (WP-52) sorts every symbol el8's glibc exports into four
buckets by what the runtime has to stand behind it, and the fourth is the stub:
a name Cygwin's export surface does not carry at all. `doc/what-the-veneer-lacks.md`
is the inventory of those names, generated and read category by category. This
document answers the question that inventory raises but does not settle. A stub
is a name with nothing behind it, so is it a hole the project will fill, or the
right answer left in place? Both. The rest of this sets out in what proportion.
Most stubs are correct and meant to stay; a minority are work not yet done; a few
have already stopped failing.

## What a stub is, in the binary

A stub is not a missing symbol. A missing symbol breaks the link: the dynamic
loader cannot resolve it, and the program never starts. A stub is present. Its
name sits in `libc.so.6`'s dynamic symbol table, bound to the same glibc version
node the real library would bind it to (`__ctype_b_loc` under `GLIBC_2.3`,
`epoll_wait` under `GLIBC_2.3.2`), so the version lookup the loader performs at
startup finds exactly what it expects, and resolution succeeds. What differs is
the body. WP-53 binds a stub to a body that reports its own absence rather than
pretending to work, in the words DR-0052 sets down. The call links, loads, and
dispatches like any other. It fails only when run. The failure never varies.

That distinction is the whole reason the fourth bucket is a bucket and not an
omission. Leaving the name out would move the failure from a defined runtime
error to a link error, triggered the moment any consumer so much as references
the symbol, even one that never calls it on a live path. Keeping the name, at its
correct node, holds the shape of the interface and confines the failure to the
one call that actually needs the absent function.

## The four fates of a stub

Not every stub is the same kind of absence, and the bucket count alone hides the
difference. A row in the fourth bucket is on one of four tracks.

**Correct and permanent.** Most of the bucket is glibc's own internals: the
`_IO_*` libio symbols behind `FILE`, the underscore and double-underscore
helpers glibc uses to call itself, the `__res_`/`ns_` resolver machinery, the
`ld.so` loader hooks, the `__*_finite` and `__*_chk` variants whose base function
is itself absent. A conforming program names none of these. It reaches `FILE`
through `fopen` and `fprintf`, not through the `_IO_` symbols underneath. These
stay stubs. That is right: filling them would answer a question no well-formed
caller asks.

**Bounded by the host.** The absences that bite are the public Linux interfaces
Cygwin never had, because they are kernel calls with no Windows equivalent its
wrappers could sit on: `epoll_wait`, `inotify_init`, `eventfd`, `signalfd`,
`timerfd_create`, `statx`, `splice`, `memfd_create`, and their kin. A program
that reaches for `epoll` does not run here. That is not a defect. It is a property
of the platform, the same statement Linux would make about a call its kernel does
not implement. Some of these could be rebuilt on top of what Windows does offer,
at real cost; most will stay stubs, because the semantics do not map.

**Not yet wired.** A few stubs are stubs only because no work package has yet put
a body behind a function the platform could support. `getauxval` is the clear
case: WP-40 already builds the auxiliary vector the call would read, so the answer
exists. Nothing reads it out yet. These are the genuine to-do rows. When a later
work package wires one, it moves to forward or shim, and `t/reproduce.sh` shows
the fourth-bucket count fall by exactly that many. The check does double duty: the
drift guard that keeps the inventory honest is also what measures the burn-down.

**Filled.** DR-0052 opened a fourth track. A stub whose answer is fixed by the
language and computable exactly can be given a synthesized body rather than left
to fail. The first is the ctype family, `__ctype_b_loc` and its case-map kin,
whose tables the C and POSIX locales define entry for entry. `gen-ctype-table.py`
synthesizes all three tables and certifies them byte-for-byte against glibc's own,
run on the pinned el8 image. The name stays in the fourth bucket, because the
classification records one fact only, whether the export surface carries the name,
and it does not. But the body works. The line DR-0052 draws around this is
determinacy: a body is a candidate for filling only when it depends
on nothing the veneer lacks, which the language fixes and a differential can
check. A stub whose behavior would depend on a real system call, or on a
Cygwin-side object, stays a stub that fails until something genuine stands behind
it.

## Is this faithful to ELF and Linux?

At the ABI, yes. A stub occupies the correct symbol and the correct version node,
so everything the dynamic linker does — resolution, version binding, `DT_NEEDED`
satisfaction — behaves exactly as it does against the real glibc. A program links
and loads identically whether a given symbol forwards, shims, or stubs. That is
the fidelity the veneer exists to hold. A stub holds it as fully as a forward
does.

At behavior, the answer is layered. It is honest only when kept that way. A
failing stub does not do glibc's work; it reports that it cannot. For the
internals, that is faithful by omission, because the behavior a conforming caller
can observe is unchanged when the caller never makes the call. For the
host-bounded interfaces, it is faithful to the platform's own reality: Cygwin over
Windows genuinely lacks `epoll`, and a stub that says so is more faithful than a
body that fakes a poll loop and drifts from the real semantics under load. For a
wired or filled stub, behavior is restored to the degree the body provides, and
certified where the body is filled.

The two levels carry different weights, and the inventory says which is firmer.
Absence is a hard statement. Presence is softer. A name not on the export
surface is not callable, whatever its semantics would have been. A name that is
present may still, on inspection, behave differently from glibc's, which is what
the third bucket exists to flag. So the platform understates rather than
overstates what it provides. The stubs are the reliable floor, and some symbols
counted today as forwards will yet prove to need shims.

## Watching stubs change

Three views track the fourth bucket, none of them a number kept by hand.
`veneer/classification/bucket4-inventory.tsv` is the categorized list, and
`doc/what-the-veneer-lacks.md` is its reading; `t/reproduce.sh` regenerates both
and fails if either drifts, so a stub that becomes a forward surfaces as a count
that fell. `bin/progress.py wp-56 <slice> stub` lists the stubs in one slice
against their targets, for reading the absence at the grain of a subsystem.

One thing is still missing, and DR-0052 names it: a verdict that separates a
filled stub from a failing one. Today the acceptance harness counts
`__ctype_b_loc` against bzip2 even though the veneer answers it correctly, and
that reconciliation is left to a follow-up. Until it lands, the fourth-bucket
count is a ceiling on what fails, not a measure of it.
