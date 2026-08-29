# Proposal 0003 — load-time TLS rewriting for vendor binaries

Status: accepted in direction; spike 8 ran 2026-08-29 and the fallback holds
        over the data-movement forms and not the arithmetic ones
Date: 2026-08-29
Raised by: the operator, against `spike/ld-tls-relaxation/`

## The problem

A binary built for real Linux reaches for TLS through `%fs`, and on this host
that base does not survive a context switch. Every route the toolchain can
take fixes the code we compile and reaches none of the code we do not.

That gap is not hypothetical. WP-T4's acceptance comparison builds vendor
source against our tree, but `ldd` on a vendor binary is WP-33's exit
criterion, WP-54's is a vendor binary's `DT_NEEDED` list satisfied from our
tree, and anyone evaluating this platform will drop a prebuilt binary on it
within the hour. A `.o` from a vendor archive has the same problem and reaches
our linker rather than our compiler.

## The direction

Identify psABI TLS accesses at load and rewrite them to reach the thread
pointer through DR-0003's carrier. As a fallback for binaries we did not
build, not as the primary mechanism: our own world is compiled correctly by
the toolchain, and this exists for the rest.

## What it costs

Instructions cannot grow in place, because everything after them carries
computed relative offsets. `mov %fs:0x0,%rax` is nine bytes and the C3 chain
needs two loads in thirteen to sixteen, so most sites become a five-byte jump
to a loader-generated stub with the remainder padded. Not a call: a call
writes below `%rsp`, into the red zone this project is already arguing about.
Per-site stubs, then, and roughly ten cycles per access on top of C3's
measured 5.5.

One form is better than that and it is worth naming, because it reopens a
settled question rather than merely costing money. `mov %fs:0x0,%rax` does not
read a variable; it reads `tcbhead_t.tcb`, glibc's self-pointer, so the
instruction means "give me the thread pointer". If the thread pointer lived at
a fixed offset in the TEB, that becomes `mov %gs:0x1480,%rax` — one prefix
byte and a different displacement, exactly nine bytes, rewritten in place with
no stub at all. That is DR-0003's carrier C1 or C4, both of which passed every
persistence case and lost to C3 on ownership rather than on behavior.

It does not rescue everything. `mov %fs:-0x8,%rax`, a direct local-exec access
at a `tpoff`, reads TP−8, and `%gs`'s base is the TEB rather than the TCB;
that form still grows and still needs a stub. So the carrier question is worth
reopening only if the site census says the self-pointer form dominates, and
nobody has counted.

## What it risks

Finding every site is the hard part, and in a linked executable the local-exec
relocations are already consumed, so there is nothing to walk. Scanning bytes
puts this on the wrong side of a problem x86-64 does not solve: instruction
boundaries and code-versus-data are not decidable in general, `.eh_frame`
coverage makes linear disassembly good rather than certain, and a false
positive corrupts a working instruction.

Whether that is survivable was spike 8's question, and it ran on 2026-08-29.
A missed site faults — access violation, every time, however the base was lost
— and a vectored handler registered ahead of Cygwin's can identify the
instruction, emulate it through carrier C3 and resume, leaving the interrupted
code's other registers and its flags intact. It held where it had to: under a
burner on every processor with no call site to hook, and across 24 threads
faulting at once. So a miss is slow rather than wrong, this proposal's route is
a real fallback, and the rewriter is allowed to be a heuristic.

One finding makes the handler cheaper than this proposal assumed. With the base
at zero the effective address is the offset, so Windows hands the handler the
TLS displacement as the faulting address and no address arithmetic over the
addressing modes is needed at all; what is left to decode is a length and a
destination register.

The verdict is not unqualified, and the qualifier lands on this proposal rather
than on the spike. The handler covers the data-movement forms — `mov` both
ways, the immediate store, `movzx` and `movsx`. It refuses the read-modify-write
forms by name, because emulating `addl $1, %fs:-0x4` means emulating `EFLAGS`
and a handler that gets the carry flag wrong is worse than one that declines.
A missed site of that shape is a `SIGSEGV`, not a slow success. So the fallback
is sound over part of the instruction space and absent over the rest, and this
proposal now carries a choice nobody has priced: emulate the flags in the
handler, or require the rewriter to be exhaustive over exactly the forms it is
least able to be exhaustive about. Which is cheaper turns on the site census
below, which is no longer optional.

Cost, for the same reason. A handled fault measured 2215 ns against 0.5 ns for
the same access once rewritten. That is fine for a miss and ruinous as a
policy, which is the argument for the rewriter existing at all rather than the
handler standing alone. `spike/fs-base-fault/README.md` carries the reading and
what the measurement does not reach.

Two further costs are certain rather than conditional. Patching mapped
executable pages at load is precisely the shape `AGENTS.md` already says
endpoint protection will object to, and this makes that worse. And rewriting
dirties text pages privately, so whatever cross-process sharing survived
WP-32's mapping does not survive this.

## What is not proposed

Rewriting as the primary mechanism. The toolchain route stands: WP-12 refuses
the relocations that license an instruction rewrite, WP-13 emits its own
access sequences, and the code this project compiles never needs a rewriter.

Retiring anything in DR-0003. The carrier observation above is a question for
a census and then for the operator, not a change this proposal makes.

## Not verified

The site census, which now carries two questions rather than one. The claim
that the self-pointer form dominates is what would justify reopening the
carrier, and it is still a guess from how compilers emit local exec rather than
a count over vendor binaries. Added to it: what share of `%fs` sites in the el8
set are read-modify-write, since that share is the size of the hole the
handler leaves and the thing that decides between emulating `EFLAGS` and
demanding exhaustiveness. Counting both is one pass and the tree from
`spike/vendor-image-shape/` is already unpacked.

That the handler survives a real program. Spike 8 registered it first in a
process whose only faults were the ones it caused; a runtime shares the
vectored chain with Cygwin's own machinery under a program that faults for its
own reasons, and nothing has measured that.

The forms outside the decoder. `mov`, the immediate store, `movzx` and `movsx`
are covered; the arithmetic forms, x87 and SSE accesses, string instructions and
locked accesses are not. That list was a scope decision taken to keep the
emulation obviously correct, not a measurement that the rest do not occur.

That per-site stubs cost about ten cycles. That is arithmetic over instruction
counts, not a measurement, and it ignores the branch predictor entirely.
