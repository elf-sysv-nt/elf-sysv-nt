# fork, vfork and posix_spawn (WP-42)

Cygwin's fork copies a process by creating a suspended child of the same image
and writing the parent's memory into it. Everything the loader keeps in its own
writable data crosses on that copy for free: the object table, the search
configuration, the TLS module table, the rendezvous structure. Three things do
not, and this package is the three.

## The manifest

Address space the loader took without going through the host's bookkeeping is
not replayed. WP-32 maps an object through the host's mmap exactly so that it
is; WP-41's low window is a bare reservation, made by the parent into a
suspended child and re-reserved in pieces by `elf_window_yield`, and nothing
records it. A child that resumes without those reservations has a hole where
the ELF world lives, and the first host allocation to land in it is
unrecoverable.

So the loader records what it reserved outside the host's view, the record is
packed into bytes the child inherits as a copy, and the child replays it before
anything else runs. `manifest.c` is both halves. The packing is fixed-width and
little-endian, because the writer and the reader are the same build in the same
process family. The unpacking is written as though they were not: nothing in
the buffer indexes the buffer before it has been checked against the buffer's
length, the length must be exactly what the count occupies, and every invariant
the packer maintains is re-established rather than trusted — nonzero sizes, no
wrap, known kinds, sorted, disjoint, names terminated inside their field.

That is the only thing here that reads bytes it did not write in the same call,
and in the real fork it reads them in a child that has repaired nothing yet, so
it is fuzzed: mutated, truncated and extended manifests against a guard page
under the undefined-behaviour sanitizer, with the output array between canaries
and every acceptance re-derived from the bytes rather than believed.

## The bracket

The loader lock is the one that bites. A mutex held by a thread that does not
exist in the child is held forever, and a fork from a thread inside `dlopen` is
what produces one.

Prepare handlers run in the reverse of registration, then the lock is taken, so
it is the innermost thing held across the call: the forking thread cannot get
past it until whoever is in the loader has left, and no other thread can enter
after. The parent releases and runs parent handlers in registration order.

The child does not release. A recursive mutex records an owner, and the child's
only thread is a copy of the thread that took the lock rather than that thread,
so unlocking on its behalf is not defined; the child initializes over the lock,
which leaves it unheld and owned by nobody. Then the manifest, then the thread
pointer, then the audit, and only then the child handlers. DR-0029.

## The thread pointer

DR-0003's carrier is keyed to `NtTib.StackBase`. The child's initial thread is
not the parent's thread, so the carrier word is at a different address and holds
whatever the copy put there. `elfsysv_tp_reestablish` writes it back, which is
what WP-30 delivered it for and what this is its only caller for.

The TCB is an argument to the prepare call rather than a field of the fork
state. A process has one loader and one TLS layout but a TCB per thread, and the
first version of this package kept it in the state; the certification found it
by forking from the main thread after an earlier stage had forked from a
managed thread that was since joined and freed, and the child faulted
re-establishing from it.

## The audit

"The loader crossed intact" is not something a child can assert about itself. It
is a comparison, and `audit.c` is the thing compared: the object list with every
object's bias, dynamic section and mapping, the search configuration, the static
TLS layout, this thread's whole DTV slot by slot, the `r_debug` structure and
its address, and the loader's own code address. The parent takes it under the
lock; the child takes it after repairing and diffs it, and the diff names the
first field that moved.

The field order is the explanation. A loader image that moved is the Cygwin
rebase failure and it moves everything derived from it, so it is checked first
and reported by name; after it the structures are reported outermost first, so
"the object table changed" precedes "a DTV slot changed" when both did.

A child whose audit does not match is refused rather than repaired, and its
child handlers do not run: a handler that calls `dlsym` against a loader that
did not cross is worse than no handler at all.

## What is certified

`t/run.sh` builds all of it and holds it to the done-when.

The unit test drives the three phases with no host fork in the loop, which is
what makes the ordering checkable at all: the handler orders in all three
phases, the lock taken after the last prepare handler and reinitialized rather
than unlocked in the child, the region table sorted and disjoint under
overlapping and wrapping and oversized input, the manifest round-tripping and
refusing every corruption named above with a reason, and the audit naming the
first field that moved with a rebase reported before anything it caused.

Then the done-when itself. A second thread loops through the real `dl_open` and
`dl_close` of WP-38's cross-linked plugin, holding the loader lock and sleeping
inside it so the window is wide rather than theoretical, while the main thread
forks through the three phases. The child calls `dlsym` on the object and calls
into it across the ABI boundary, and reports six bits — crossed, symbol found,
code ran, loader where it was, `cygwin1.dll` where it was, thread pointer its
TCB's — so 63 is the only pass. A child that deadlocked never reaches the call
and its own watchdog kills it, which comes back to the parent as a signal rather
than a status, so a hang cannot read as a slow pass.

The TLS stage forks from a managed thread, whose carrier the child's initial
thread does not have, and compares the whole DTV across. The rebase stage runs
the fork repeatedly and requires the loader's code address and the base of
`cygwin1.dll` to be where the parent left them every time; on 2026-08-30,
neither moved.

`elfsysv-fork` is the front end that calls the phases the way Cygwin's spawn
path will, since winsup is not in this tree. It runs under all three flavours,
which take the same path: Cygwin's `vfork` is a fork that promises less and its
`posix_spawn` is a fork followed by WP-41's exec branch, so the flavour travels
only as a label.

## What this package does not do

The bracket is applied by its callers rather than by `dl_open` itself. WP-38
delivered a single-threaded surface and left the per-thread error carrier to
this package's thread work; moving the acquire and release inside the `dl`
entry points is identified here and not done. DR-0029's Not verified section
carries that, and the rest of what is read rather than measured.
