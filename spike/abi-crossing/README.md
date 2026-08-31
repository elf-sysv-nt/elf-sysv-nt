# Spike 3: the ABI crossing

Can one runtime entry point present a System V face over an MS-ABI core,
survive a signal delivered mid-call, and leave the red zone intact? The first
two, yes. The third, no, and what takes the red zone away is not Windows.
`results-2026-08-29.txt` is the transcript and the reading is below it.

**Gates.** The ABI boundary, which is to say `elfsysv1.dll` itself. A no would
have sent the program to the veneer-thunk fallback named in the breakdown,
which is a decision rather than a task. It did not.

Three files take the measurement. `crossing.S` holds the two callers and the
red-zone watcher, `crossing.c` holds the faces they call and the eleven cases,
and `abi-crossing.sh` builds them, asks the compiler one question of its own,
and writes the transcript. What is kept here is the means of taking the
measurement again.

## Running it

    ./abi-crossing.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted. A full run is about a minute.
`--terse` prints the summary block alone, one `key=value` per line, which is
the form to quote in a document. `--case NAME` runs one case. `--depth N` sets
how many bytes below `%rsp` the watcher paints, `--rounds N` how long the
quiet cases spin, and `--events N` how many signals, hijacks and faults the
provoked ones get.

`t/run-tests.sh` checks the spike itself. Twenty-two checks, all green on
2026-08-29.

## Why two of these are hand-written

`sysv_caller_probe` and `ms_caller_probe` are assembly because the thing being
measured is the compiler's own convention thunk, and a caller the compile
wrote would emit that thunk on both sides of the check. A check expressed in
terms of what it checks measures nothing. So each one loads the arguments the
way the psABI or the Microsoft x64 document says to, poisons every registe
its convention obliges the callee to preserve, and reports which ones came
back changed.

`redzone_watch` is assembly for a harder reason. It has to be a leaf that
makes no call at all: the moment it calls anything the return address lands
eight bytes below `%rsp`, inside the hundred and twenty-eight the psABI
reserves, and the watcher has destroyed what it came to watch. So it paints
the region with a pattern that encodes each word's distance from `%rsp`,
reads it back forever, and counts what moved. Nothing it does can account fo
a change, which is what makes a change mean something.

The watcher is entered through a `sysv_abi` frame, because the question is
about a signal arriving in the middle of a System V call and not about Windows
in general.

## The cases

Six have to pass, two are controls, and three are measurements with no pass o
fail to give.

| case | kind | asks |
|---|---|---|
| `sysv-face` | crossing | sixteen arguments in System V registers, five down-calls into Windows, callee-saved set intact |
| `ms-face` | crossing | a Microsoft caller into System V code, with `%rsi`, `%rdi` and `%xmm6`-`%xmm15` intact |
| `varargs` | crossing | a System V variadic entry unpacked and repacked into the runtime's own `snprintf` |
| `varargs-raw` | control | the same list read the way a Microsoft-shaped reader would; has to come out wrong |
| `callbacks` | crossing | Windows calling in four ways, each reaching System V code one frame down |
| `fault-through` | crossing | a null store in Microsoft code under a System V frame, recovered through Cygwin |
| `rz-quiet` | control | the watcher spinning undisturbed; has to see nothing |
| `rz-preempt` | redzone | a burner on every processor and no other event |
| `rz-hijack` | redzone | suspend, read the context, write it back, resume |
| `rz-signal` | redzone | an asynchronous Cygwin signal into the spinning leaf |
| `rz-veh` | redzone | a hardware fault dispatched by Windows and stepped over by a vectored handler |

`rz-quiet` is the control the four measurements below it lean on, and it is
judged rather than merely recorded: a watcher that has gone blind reports the
same zero as a red zone nobody touched.

`rz-signal` and `rz-veh` are a pair for the same reason `huge` and `hugehigh`
were a pair in spike 2. Either alone would say the red zone dies and leave the
useful half of the question open. Together they say which layer kills it.

## The verdict, 2026-08-29

`verdict=yes`, and the red zone is gone, and those two are less related than
they look.

**The crossing holds in both directions.** A System V caller passed six
integers, eight doubles and two stack arguments into a `sysv_abi` entry point
that then called `GetCurrentThreadId`, `VirtualQuery`, `QueryPerformanceCounter`,
the runtime's `snprintf` and `Sleep` -- five descents into Microsoft x64 -- and
returned a value derived from all sixteen arguments, with `%rbx`, `%rbp` and
`%r12` through `%r15` untouched. Going the other way, a Microsoft calle
reached System V code through a `ms_abi` entry with all eight callee-saved
GPRs and all ten callee-saved XMM registers intact. That second one is the
direction the design was nervous about, because `%rsi`, `%rdi` and `%xmm6`
through `%xmm15` are callee-saved to a Windows caller and volatile to a System V
callee, and a thunk that forgets one of them leaks the convention out of the
bottom of the DLL into a caller that will never know. Nothing leaked.

**Windows calls in, and the calls land.** A thread start, a queued APC, a
vectored exception handler and a Cygwin signal handler each reached System V
code one frame down and came back. This is the treacherous set the breakdown
names, and at one function's width it is not treacherous.

**A fault under a System V frame comes back as a signal.** A store through a
null pointer inside Microsoft code, one frame beneath a `sysv_abi` frame,
reached Cygwin's handler as SIGSEGV and the handler left by `siglongjmp` past
the System V frame. Afterwards the crossing was exercised again and still held.
That is the claim AGENTS.md forbids assuming, tested at the only width anyone
has tested it.

**Variadic entry points need their own machinery, and no forwarding is
possible.** Plain `va_start` inside a `sysv_abi` function is a category erro
on a Microsoft-ABI target rather than a portability wrinkle: gcc's `va_list`
here is the Microsoft one, eight bytes, and the System V one is a
twenty-four-byte descriptor reached through `__builtin_sysv_va_list`. Handing
the second to a reader shaped for the first fetches the pair of offsets at its
head as the first argument -- 206158430216 where 111 was passed. So every
variadic export in the veneer unpacks its arguments one at a time and passes
them on by value. `printf` cannot be a tail call.

**Nothing that happens inside the kernel touches the red zone.** Preemption
with a burner on every processor, over two hundred thousand passes, moved
nothing. Two thousand rounds of suspend, `GetThreadContext`, `SetThreadContext`
and resume -- which is Cygwin's signal-delivery mechanism with the delivery
taken out -- moved nothing either.

**Windows' own exception dispatch leaves the red zone alone, and does so with
room to spare.** Two thousand hardware faults in the watching leaf wrote
nothing above 320 bytes below `%rsp`. The dispatch record starts there and
runs down past the probe's own handler frames. Three hundred and twenty is not
a promise, and the number moves with the alignment of the interrupted `%rsp`
-- it measured 304 in a run where the watcher's frame sat differently -- but it
is nowhere near the 128 the psABI asks for. This was worth measuring for its
own sake: a spike watching only 128 bytes would have reported that Windows
respects the red zone, which is true and useless, because it cannot tell a gap
Windows leaves on purpose from one it happens to miss.

**Cygwin's signal delivery destroys it, from the first word.** An asynchronous
signal into the same leaf lost the word at `%rsp-8` and everything down to the
1024 bytes watched. Cygwin hijacks the thread and builds the handler's call
frame at the interrupted stack pointer, so the eight bytes the psABI reserves
first are the eight bytes it takes first. Nearest offset 8, on every one of two
thousand deliveries.

So the red zone dies, and it dies at our layer rather than at the host's.
`redzone_policy=mno-red-zone required` stands, and the reason it stands is
worth carrying: the code that breaks the guarantee is Cygwin's own delivery
path, which this project already intends to modify. That is a second option
beside the flag, and picking between them is not an agent's to do.

**The flag is not free.** gcc gives a `sysv_abi` leaf a red zone on this
target: asked for a leaf with four locals it emitted `movq %rdi, -32(%rsp)`
and never adjusted the stack pointer, and `-mno-red-zone` turned that into
`subq $32, %rsp` and positive offsets. So `-mno-red-zone` throughout the DLL
is a flag that changes code generation rather than a restatement of the
default, and the cost is the one the psABI always said it was: a stack
adjustment in every leaf that would not otherwise need one.

## What this does not answe

One function's width, which is what the milestone asked for and no more. The
runtime here is Cygwin's, unmodified, called normally; nothing was rebuilt with
a System V export surface, and the claim in the breakdown's Not verified
section is narrowed rather than settled.

Unwinding. `siglongjmp` past a System V frame is not the same as
`RtlUnwindEx` through one, and MS-format unwind data for `sysv_abi` frames was
never inspected. C++ exceptions crossing the boundary were not tried at all.
`fault-through` says Cygwin's signal path survives a System V frame on the
stack; it does not say a Windows unwinder could walk one.

`DllMain`, PE TLS callbacks, and thread starts *into a re-faced DLL*. The
callbacks case has Windows entering System V code inside an ordinary
executable. A DLL whose export surface is System V is entered by the loade
before any of this spike's machinery exists, and that is WP-41's neighbourhood.

What Cygwin's delivery would cost if it reserved 128 bytes. The finding points
at a repair and nobody has priced it.

Whether the register checks would catch a partial leak. They catch a total one
-- `t/run-tests.sh` builds the probe against a callee that destroys everything
and watches all eighteen bits light -- but a thunk that saved seven of eight
registers was never constructed.

One Windows build, one processor, one compiler: `CYGWIN_NT-10.0` under the
pinned 2019 root, twelve processors, gcc 7.4.0. Spike 1's answer was also from
one machine and it decided a layer, so the narrowness is worth stating twice.

## The fault-through characterization, 2026-08-31

`fault-through` passes on Cygwin 3.0.7 and fails on 3.6.10, and `issue/0001`
reads that as 3.6.10 delivering a fault beneath a System V frame differently.
It does not. The case as written cannot support that reading, because it
reports the same sentence -- "the fault never came back as a signal" -- fo
three different things: a fault that was never raised, a fault that was raised
and lost, and a fault that came back wrong.

`characterize-fault-through.sh` separates them. It builds `fault-probe.c` at
three optimization levels and runs each case in its own process, because a case
that loses the fault takes the process with it and a matrix printed by a
process that died is one row long. The transcripts are
`results-fault-through-3.6.10-2026-08-31.txt` and
`results-fault-through-3.0.7-2026-08-31.txt`, and the comparison is the point.

**The two hosts agree.** Every one of the six shapes -- a fault in Microsoft
code beneath a non-leaf System V frame, reached directly or through a pointer,
with nothing, one plain frame or two in between -- recovers on both roots at
every optimization level. So do all three controls. `finding=crossing-holds` on
3.0.7 and `finding=specimen-deleted-crossing-holds` on 3.6.10, and the only
difference between those two words is the specimen.

**The compiler removes the fault.** The spike raises it with
`*(volatile int *)0 = 1` inside `ms_faulter`. A store through a literal null
pointer is undefined behaviour, so gcc 14 concludes the path is unreachable and
drops the *call site* that reaches it -- not merely the store. Measured by
symbol rather than by instruction pattern: at `-O1` and `-O2` the built binary
contains no call to the System V faulter at all, and the probe's `null-store`
case answers `nofault`, meaning it ran and nothing faulted. gcc 7.4 keeps the
call at all three levels. So on 3.6.10 the case reports that the fault did not
come back because the fault does not happen.

**The unwind seam is unchanged.** DR-0012 rests on a System V frame carrying no
host unwind record, and that record was measured on gcc 7.4. Re-measured here
through `RtlLookupFunctionEntry` under gcc 14: an `ms_abi` non-leaf and a plain
non-leaf each have a `RUNTIME_FUNCTION`, and a `sysv_abi` non-leaf has none.
DR-0012's tripwire has not tripped.

So `spike/abi-crossing`'s `yes` is not superseded by the environment switch.
What is owed is a specimen the compiler cannot reason away -- the same repai
`runtime/core/t` needs, and the reason `runtime/signal/t` never had the problem,
since it raises its fault with `ud2` in hand-written assembly. Making that
repair is not this measurement's to make.

## Repaired and re-verified, 2026-08-31

The operator directed the repair and it is in. `ms_faulter` reads its address
from a volatile global -- still zero, no longer provably so -- and the
faulting call sits one plain frame below the `sigsetjmp`, which is a
workaround for a second gcc 14.4 defect the repair exposed: an internal
compiler error in `choose_baseaddr` when a function that calls `sigsetjmp`
also calls a `sysv_abi` function it cannot prove unreachable. The failure
message now distinguishes a specimen missing from the binary from a delivery
that lost the process, and `t/run-tests.sh` gained a guard that checks, by
symbol, that the call to the System V faulter survived compilation.
Twenty-three checks, all green on 2026-08-31.

`results-2026-08-31.txt` is the re-run on the primary root, regenerated twice
with matching findings: `verdict=yes` with every crossing case passing,
`fault-through` included, under Cygwin 3.6.10 and gcc 14.4.

One finding in it is new, and it is reported here rather than acted on.
`rz-signal` on 3.6.10 reads `nearest:320` -- three runs out of three, stock
delivery, no spike-7 reservation in play -- where 3.0.7 read `nearest:8` on
every delivery. Cygwin's own signal delivery on this version no longe
touches the psABI's reserved 128 bytes, and the transcript accordingly says
`redzone_policy=no clobber inside 128 bytes` where the 2026-08-29 transcript
said the flag was required. Both transcripts stand; they measured different
Cygwins. This bears directly on DR-0006, whose premise -- the delivery site
takes `%rsp-8` and wants repairing -- was measured on 3.0.7, and whether that
record is reaffirmed against 3.6.10's source or reopened is the operator's,
not this spike's. Note also what this does not say: `nearest:320` is an
observation about one version's delivery path, not a contract, and the
hand-off of the whole question to WP-43's measurement against Cygwin's real
`sigdelayed` is unchanged.

## Not verified

That the six shapes exhaust the ways the fault can be reached. They are the
ones that varied while the characterization was being written, and they all
recover. Earlier throwaway reductions written during the same work *did* lose
the fault on both roots, and those losses stopped reproducing once the probe
was written carefully -- one case per process, the case name checked before the
`sigsetjmp`, no non-volatile local read after the `siglongjmp`. Every loss
found so far has turned out to be a defect in the reduction rather than in the
host, but "so far" is the whole of the claim, and a shape that loses the fault
for a real reason would look the same from outside.

That the transcript regenerates. It was regenerated three times on 2026-08-29
with matching verdicts and matching red-zone findings, but the pass counts and
the `rz-veh` nearest offset move between runs by design, so the transcript
reproduces in its findings rather than byte for byte. Diff it on the summary.

That 320 bytes is the gap Windows always leaves. It is what this machine did
across depths of 512, 1024, 2048 and 8192 bytes and two builds, varying by 16
with the alignment of the watched `%rsp`. It is documented nowhere and should
be treated as an observation about a version of Windows rather than a
contract.

That `rz-preempt` provoked as many preemptions as it looks like it did.
Preemptions cannot be counted from the thread they happen to, so the events
column carries burner threads instead. Spike 1 established on this same machine
that a burner per processor deschedules a spinning thread within tens of
milliseconds, and this ran for far longer than that, but the count is inferred.

That Cygwin's delivery writes only as far as 1024 bytes down. That is the
floor the watcher was looking at, not a measurement; at 4096 bytes it reached
1048.
