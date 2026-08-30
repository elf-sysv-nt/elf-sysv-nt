# DR-0029: what crosses the fork, and how the child knows

Accepted 2026-08-30. Supersedes nothing.

## Context

Cygwin's fork creates a suspended child of the same image and writes the
parent's memory into it. The copy carries everything the loader keeps in its
own writable data, so the object table, the search configuration, the module
table and the rendezvous structure all arrive in the child by construction and
need no help. Three things do not arrive that way, and they are the reason this
package exists.

Address space the loader took without going through the host's memory
bookkeeping is not replayed. WP-32 maps an object through the host's mmap
precisely so that it is; WP-41's low window is a bare reservation, made by the
parent into a suspended child and re-reserved in pieces by `elf_window_yield`,
and nothing records it anywhere the copier can see. A child that resumes
without those reservations has a hole where the ELF world lives, and the first
allocation the host satisfies out of it cannot be taken back.

The thread pointer is keyed to `NtTib.StackBase` by DR-0003. The child's
initial thread is not the parent's thread: its stack base is elsewhere, so the
carrier word is elsewhere, and it holds whatever the copy happened to put
there. WP-30 delivered `elfsysv_tp_reestablish` for this moment and named this
moment as its only caller.

The loader lock is the one that bites. A mutex held by a thread that does not
exist in the child is held forever, and a fork from a thread that is inside
`dlopen` is precisely the case that produces one. This is the failure the plan
names in the done-when, and it is not hypothetical: it is the standard reason a
forking process with a dynamic loader hangs.

## Decision

The loader keeps a manifest of the reservations the host does not know about.
The parent packs it into bytes before the fork; the child parses it and replays
it before anything else runs. The manifest is fixed-width and little-endian
because the writer and the reader are the same build in the same process family
and a self-describing format would only add cases to validate. It is parsed as
hostile input regardless: nothing in the buffer indexes the buffer before it
has been checked against the buffer's length, the length must be exactly what
the count occupies, and every invariant the packer maintains — nonzero sizes,
no wrap, sorted, disjoint, terminated names — is re-established by the unpacker
rather than trusted. It is fuzzed against a guard page under the
undefined-behaviour sanitizer for the same reason: in the real fork it is read
in a child that has repaired nothing yet, and there is nothing behind it.

The bracket is POSIX's, and both halves belong to this package. User prepare
handlers run in the reverse of registration order and then the loader lock is
taken, so the lock is the innermost thing held across the call and the forking
thread cannot proceed until whoever is in `dlopen` has left. The parent
releases and runs parent handlers in registration order. The child does not
release. A recursive mutex records an owner, and the child's only thread is a
copy of the thread that took the lock rather than that thread; unlocking on its
behalf is not defined. The child initializes over the lock instead, which
leaves it unheld and owned by nobody, and only then repairs the rest.

The forking thread's TCB is an argument to the prepare call rather than a field
of the loader's fork state. A process has one loader and one TLS layout but a
TCB per thread, and a state that carried one across the life of the process
would eventually name a thread that had exited.

What crossed is checked rather than asserted. The parent reduces its state to
one record — the object list with every object's bias, dynamic section and
mapping, the search configuration, the static TLS layout, this thread's whole
DTV slot by slot, and the `r_debug` structure with its address — and the child
takes the same record and diffs it. The diff names the first field that moved.

## Consequences

The DLL rebase failure mode that haunts Cygwin's fork is confirmed absent by
measurement rather than assumed absent, because the loader's own code address
is one of the audited fields and it is audited first. A rebase moves it and
moves everything derived from it, so reporting it before the differences it
caused is what makes the diagnostic useful rather than merely correct. The
certification also has every child compare the base of `cygwin1.dll` against
what the parent recorded. Over the runs taken on 2026-08-30 neither moved.

The audit is a comparison, not a repair. A child whose state does not match is
refused rather than fixed, and its child handlers do not run, because a handler
that calls `dlsym` against a loader that did not cross is worse than no handler
at all. That is a deliberate choice to fail loudly at the one moment the
failure is still explainable.

The manifest and the audit both carry a bound. A link map longer than the walk
limit is a defect in the map rather than a program with that many objects, and
the audit stops rather than following a cycle into the child's only chance to
report anything.

`vfork` and `posix_spawn` take this path unchanged. Cygwin's `vfork` is a fork
that promises less and its `posix_spawn` is a fork followed by WP-41's exec
branch; neither has state of its own to cross, so the flavour travels only as a
label a diagnostic can print.

## Not verified

The loader lock is modelled in the certification rather than held by
`dl_open` itself. WP-38 delivered a single-threaded surface and deferred the
per-thread error carrier to this package's thread work; the bracket is
therefore applied at every entry into the loader by the driver, exactly as the
surface will hold it, but the surface does not yet hold it itself. Moving the
bracket inside `dl_open` and `dl_close` is work this package identifies and does
not do.

winsup is not in this tree, so the three phases are certified through
`elfsysv-fork`, a front end that calls them as Cygwin's fork will. The claim
that Cygwin's fork calls them at those three points is read from its source
rather than measured against a modified `cygwin1.dll`.

The rebase result is a measurement on one machine on one day, over a few dozen
forks with one object loaded. It is evidence that the failure mode is not
routine here; it is not evidence that it cannot happen under a different DLL
population or under an address-space layout randomization setting this machine
does not use.

Descriptor inheritance, close-on-exec, signal disposition and the working
directory are the spawn path's, not this package's, exactly as WP-41 left them.
