# What happens when code reads a zeroed `%fs` base?

Run 2026-08-29, and the answer is that it faults and a handler can resume from
it. `results-2026-08-29.txt` is the transcript, `measure-fs-base-fault.sh`
regenerates it, and `t/run-tests.sh` checks the spike rather than the host.

Spike 1 measured the base and found it zero after anything that deschedules the
thread. It did not measure the next instruction. That gap is small and it
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

## What came back

The first branch, on every case. Ten of spike 1's twelve events lose the base
and an access through it afterwards raised `0xc0000005` every time — a read at
`0x0`, or a write at the store's own offset. The other two are the two spike 1
recorded as surviving, `syscall` and `apc`, and no access was made through a
base that was still good. Nothing read through a zeroed base anywhere in the
run.

A vectored handler registered first sees the fault ahead of Cygwin's own
machinery, and the interesting part is what Windows hands it. **With the base
at zero the effective address is the offset**, so `ExceptionInformation[1]` is
the TLS displacement itself and the handler never has to compute an address:
`0x0`, `0x40`, `-0x8` and `-0x18` were each reported as themselves. What is
left to decode is the instruction's length and its destination, which is a much
smaller problem than address arithmetic over every addressing mode.

Nine forms were decoded, emulated through DR-0003's carrier C3, and resumed —
absolute `disp32` at nine bytes, the same negative and positive, the initial-exec
register-indirect form at four, base-plus-displacement at five, a 32-bit load at
eight, `movzwl` at nine, a 64-bit store at nine, and a store-immediate at
thirteen. Each was checked twice over: the value the resumed code saw against
the value the block held, and the handler's resume address against a landing
pad the probe computed with a `lea` in the same breath as the access. A
decoded length that was wrong would not be a wrong answer; it would be a dead
process, and `t/run-tests.sh` builds a copy one byte short to show that.

The interrupted code gets its registers back. The destination is written and
everything else — neighbouring registers, and the carry flag — came through the
fault and the resume intact.

The case that decides whether the handler has to exist at all is the one with
no call site. Under a burner on every processor, 1,304,000 reads through a
zeroed base: every one faulted, every one came back correct. Across 24 threads
at once, 15,662,000 more, none wrong; one handler serves concurrent faults on
threads each holding their own carrier and their own block.

Exactly one form was refused, and on purpose. `addq $1, %fs:-0x28` is a
read-modify-write, emulating it means emulating `EFLAGS`, and a handler that
gets the carry flag wrong is worse than one that declines. The handler returns
it to Cygwin and it arrives as `SIGSEGV`. One refusal in 17,166,025 faults,
which is the one the probe aimed at it.

A handled fault costs 2215 ns against 0.5 ns for the same access once a
rewriter has reached it, about 310,000 handled faults a second. The two numbers
are not the same kind of measurement — the fault is serial by construction and
the rewritten access is timed at throughput — so the ratio is the most generous
reading available to the rewriter, and the honest claim is the order of
magnitude: three, not one.

## What it means, and the one qualifier

The rewriter may be a heuristic **for the forms this handler covers**, which
are the data-movement forms. A miss on one of those is slow and correct, and
the subsystem WP-31 through WP-38 inherit has a defined failure mode.

It is not the whole of the claim. A missed read-modify-write site is not
repaired by this handler; it is a `SIGSEGV`, and compilers do emit
`addl $1, %fs:-0x4` and its relatives. So the fallback is sound over part of
the instruction space and absent over the rest, and closing that gap is a
choice between two costs nobody has priced: emulating `EFLAGS` in the handler,
which is a correctness liability of its own, or requiring the rewriter to be
exhaustive over exactly the forms it is least able to be exhaustive about.
Naming that gap is this spike's second deliverable and the reason the verdict
is not simply "yes".

## Not verified

The census. Which forms actually appear in vendor binaries and in what
proportion is uncounted, so the share of a real program's TLS accesses that
falls in the refused set is unknown. Proposal 0003 already carries this as a
cheap count against the tree `spike/vendor-image-shape/` unpacked, and it is
now the count that decides how much the qualifier above costs.

The refusal set itself. The decoder covers `mov` in both directions, the
immediate store, and `movzx` and `movsx`; it does not cover the arithmetic
forms, the x87 and SSE forms, string instructions, or locked accesses. That
list is a decision about scope taken to keep the emulation obviously correct,
not a measurement that the rest do not occur.

The cost at scale. 2.2 microseconds is fine for a miss and catastrophic as a
policy, and nothing here measures what share of a program's accesses a rewriter
would miss. The number prices one fault, not one program.

The carrier. C3 here is spike 6's stand-in — a word below the stack base —
rather than Cygwin's real `_my_tls`, which is the same carried risk DR-0003
records and WP-2x re-measures.

Coexistence. The handler was registered first in a process whose only other
faults were the ones the probe caused. A real runtime shares the vectored chain
with Cygwin's own exception machinery under a program that takes faults for its
own reasons, and nothing here measures that.

The zero. Every finding above, the address-is-the-offset one especially, rests
on the base being exactly zero rather than merely wrong. Spike 1 measured zero
in every case and this run saw zero in every case, on this Windows. A build
that left a stale base behind would make the faulting address `base + offset`
and give the handler an arithmetic problem it does not currently have.

## Where the verdict goes

`doc/proposals/0003-vendor-binary-tls-rewriting.md`, which is written against
this measurement rather than ahead of it, in the way DR-0001 and DR-0003 were.
`doc/IMPLEMENTATION-PLAN.md`, in whichever loader package inherits the
rewriter. `doc/milestones.md`, spike 8.
