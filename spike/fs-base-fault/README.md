# What happens when code reads a zeroed `%fs` base?

Not yet run. This directory holds the question and the method; the script and
its transcript arrive when it runs.

Spike 1 measured the base and found it zero after anything that deschedules
the thread. It did not measure the next instruction. That gap is small and it
decides how good a load-time binary rewriter has to be, which is a much larger
question than it sounds.

## Why it matters

Vendor binaries are built for real Linux and reach for TLS through `%fs`. We
are not going to rebuild Red Hat's tree, so a binary that arrives already
linked has psABI TLS sequences in it and no toolchain choice of ours reaches
them. The operator's direction, taken 2026-08-29, is that load-time rewriting
is the fallback for exactly that case; `doc/proposals/0003-vendor-binary-tls-rewriting.md`
carries the shape and its costs.

The hard part of a rewriter is not rewriting. It is finding every site. In a
linked executable the local-exec relocations have already been consumed, so
there is nothing to walk and the rewriter is reduced to scanning bytes, and
code-versus-data discrimination on x86-64 has no sound general answer. Full
`.eh_frame` coverage makes linear disassembly good. It does not make it
certain.

So the rewriter will miss sites, and what a missed site costs is the whole
question:

If an access through a zeroed base takes an access violation, a vectored
exception handler is a correctness net. A miss becomes a fault the runtime
catches, emulates through the `%gs` carrier, and resumes — slow, and correct.
The rewriter is then an optimization over a sound fallback, and a heuristic is
allowed to be a heuristic.

If it reads something instead, the rewriter has to be exhaustive, nothing on
this platform can make it so, and the fallback narrows to binaries we built
ourselves with `--emit-relocs`. That is close to no fallback at all, since a
binary we built is a binary we could have compiled correctly.

## Method

Reuse spike 1's cases rather than inventing new ones. `spike/fs-base-persistence/`
already has twelve ways to lose the base, the spinning one included, which is
the case that matters most here because it has no call site to hook.

1. Write a base with `WRFSBASE` and confirm `%fs:0x0` reads it back, which is
   spike 1's own precondition and fails the run if it does not hold.
2. Deschedule by each of spike 1's cases.
3. Read `%fs:0x0` and record what came out: a value, or an exception, with its
   code and the faulting address.
4. Install a vectored exception handler and repeat. Record whether the handler
   sees the fault, whether the faulting instruction can be identified from the
   context, whether the access can be emulated through the C3 carrier, and
   whether advancing `RIP` past it and continuing produces the right value.
5. Time a handled fault, so the cost of the fallback path is a number rather
   than an adjective.

Step 4 is the one the answer turns on. A fault nobody can resume from is the
same outcome as garbage, dressed differently.

## What decides it

Faults, and the handler can fix up and continue: the rewriter may be a
heuristic. Load-time rewriting is a real fallback for vendor binaries and
WP-31 through WP-38 gain a subsystem with a defined failure mode.

Faults, and the handler cannot resume: the fallback is a diagnostic rather
than a repair. Vendor binaries with TLS do not run, and the tree should say so
plainly instead of leaving people to discover it.

Reads something: the fallback narrows to what we built ourselves, which
returns the whole question to the toolchain and makes the operator's
acceptance of the rewriting route worth revisiting.

## Where the verdict goes

`doc/proposals/0003-vendor-binary-tls-rewriting.md`, which is written against
this measurement rather than ahead of it, in the way DR-0001 and DR-0003 were.
`doc/IMPLEMENTATION-PLAN.md`, in whichever loader package inherits the
rewriter. `doc/milestones.md`, spike 8.
