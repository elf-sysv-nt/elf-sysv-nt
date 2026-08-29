# Proposal 0003 — load-time TLS rewriting for vendor binaries

Status: accepted in direction, pending spike 8
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

Whether that is survivable is spike 8's question and the reason this proposal
waits on it.

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

Everything downstream of spike 8, which has not run.

The site census. The claim that the self-pointer form dominates is what would
justify reopening the carrier, and it is a guess from how compilers emit local
exec rather than a count over vendor binaries. Counting it is cheap and the
tree from `spike/vendor-image-shape/` is already unpacked.

That per-site stubs cost about ten cycles. That is arithmetic over instruction
counts, not a measurement, and it ignores the branch predictor entirely.
