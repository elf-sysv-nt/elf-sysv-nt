# Spike 7: reserving the red zone at delivery

Spike 3 found that the red zone dies, and that our own layer kills it: Cygwin's
signal delivery hijacks the thread and builds the handler's frame at the
interrupted stack pointer, taking `%rsp-8` first, on every delivery. This spike
asks the sequel. Can a delivery path reserve the 128 bytes the psABI holds
sacred before it builds that frame, so a `sysv_abi` leaf keeps its red zone
across a signal -- and does the handler still run, and the interrupted
computation still finish correctly on the far side. Yes, to all three.
`results-2026-08-29.txt` is the transcript and the reading is below it.

**Gates.** Whether `-mno-red-zone` can be retired. It does not gate the flag as
the standing policy -- that stands either way, and WP-13 and WP-16 carry it --
and it gates nothing that starts now. What it decides is whether WP-43 can carry
an ELF-faithful repair that honors the red zone at the delivery site, the way a
Linux kernel does, and let the flag come off; or whether the flag is permanent
and the repair is not worth its price. This spike says the reservation is
possible and the far side survives it. What it costs in Cygwin's real path is
the other half of the operator's choice, and it belongs to WP-43.

Three files take the measurement. `redzone.S` holds the two no-call watchers and
the delivery stub, `redzone.c` holds the driver and the seven cases, and
`redzone-delivery.sh` builds them and writes the transcript. What is kept here is
the means of taking the measurement again.

## Running it

    ./redzone-delivery.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted; a full run is a second or two.
`--terse` prints the summary block alone, one `key=value` per line, which is the
form to quote in a document. `--case NAME` runs one case, `--depth N` sets how
many bytes below `%rsp` the watchers paint, `--rounds N` how long the quiet case
spins, and `--events N` how many deliveries each measured case gets.

`t/run-tests.sh` checks the spike itself. Twenty-one checks, all green on
2026-08-29.

## Why this can be measured without rebuilding Cygwin

The tempting reading of "fix the delivery path" is "rebuild `cygwin1.dll` with a
patched `sigdelayed`," which is a program of work rather than a spike. It is not
what the question needs. Spike 3 already built the delivery mechanism it was
measuring against: `rz-hijack` suspended the thread, read its context with
`GetThreadContext`, wrote it back with `SetThreadContext`, and resumed, which is
Cygwin's delivery with the delivery taken out. It moved nothing, because it
never built a frame.

This spike puts the frame back, two ways, and drives it from that same hijack
rather than from Cygwin's real path. It redirects the interrupted thread's
`%rip` to a handler stub and lays the handler's frame down at a stack pointer it
chooses: at the interrupted `%rsp`, which is the naive construction and has to
clobber the red zone, or 128 below it, which is the repair and must not. Both are
models of delivery, not Cygwin's delivery, in the same way
`spike/gs-thread-pointer/` measured a stand-in for `_my_tls` rather than the real
block. The stand-in is the point of a spike and the limit of one at once: it
prices the repair before WP-43 is written, and WP-43 re-measures the real
`sigdelayed` against what it found. That re-measurement is named in DR-0003's
neighbour and is not argued away here.

## The mechanism

The driver plays the kernel, on both sides. It suspends the watcher, and while
the thread is stopped it chooses the stack pointer the handler frame is built on:
the interrupted `%rsp` for the naive construction, 128 below it for the repair,
or a separate alternate stack. It points `%rip` at the stub and resumes. The stub
takes `%rsp-8` first -- the word Cygwin's `sigdelayed` takes before it builds
anything -- runs the C handler, records that it returned, and spins. The driver
waits for that, suspends the thread again, and restores the whole saved context,
which is this model's sigreturn. Registers come back exact; what the delivery
wrote below `%rsp` does not, which is what the watcher is there to see. A leaf
that kept scratch only in a register would survive any delivery for free, so the
measurement that matters is the memory the watcher reads back, not the registers
the sigreturn restores.

## The watchers

Two, and both are leaves that make no call at all, for the reason spike 3 spelled
out: the moment a watcher calls anything, the return address lands eight bytes
below `%rsp`, inside the hundred and twenty-eight the psABI reserves, and the
watcher has destroyed what it came to watch.

`redzone_watch` carries over from spike 3. It paints the region below `%rsp` with
a pattern encoding each word's distance from the stack pointer, reads it back,
and reports the nearest offset that moved. Nothing it does can account for a
change, which is what lets a change mean something.

`redzone_accum` is new, and it answers the harder half of the question. It
commits a value to the first red-zone word, drops every register copy, waits out
a window, then reads the word back and folds it into a running checksum. Across
that window the red zone is the only place the value exists, so a delivery that
scribbles the word corrupts a computation and not merely a byte, and no surviving
register can heal it. The driver aims the integrity deliveries at that window on
purpose: a signal arriving while a leaf relies on its red zone is exactly the
case the red zone exists to protect.

Both watchers record the `%rsp` they run on, and the driver measures every offset
from it. They are entered through `sysv_abi` frames, because the guarantee under
test is the System V one.

## The cases

Two are controls, and the rest are the measurement. As in spike 3, the controls
come first in the order and the verdict leans on them, because a spike that runs
only the cases it expects to pass has measured its own optimism.

| case | kind | asks |
|---|---|---|
| `quiet` | control | the watcher spinning undisturbed sees nothing; a blind watcher reports the same zero as an intact red zone |
| `deliver-naive` | control | a handler frame built at the interrupted `%rsp` clobbers the red zone, nearest offset 8; reproduces spike 3's finding through this model and proves the model matches the real path |
| `deliver-reserved` | measurement | a handler frame built 128 below the interrupted `%rsp` leaves the red zone intact, nearest offset past 128, across every delivery |
| `resume-integrity` | measurement | after a reserved delivery the interrupted leaf finishes with the value its red-zone scratch held, so the reservation preserved the computation and not merely the bytes |
| `handler-ran` | measurement | the redirected `%rip` actually reached the handler and returned, so `deliver-reserved` measured a delivery rather than a delivery that silently did nothing |
| `nested` | measurement | a second delivery while the handler runs reserves its own 128 below the handler's own `%rsp`, leaving the outer frame's red zone untouched |
| `altstack` | measurement | delivery onto an alternate stack leaves the interrupted frame's red zone untouched, which it must, and still lands and returns |

`quiet` and `deliver-naive` together are the pair that makes the rest legible:
the first says the watcher can see, the second says this model destroys the red
zone exactly where Cygwin's real path does, so a clean `deliver-reserved` is the
reservation working rather than the model having gone quiet.

## The verdict, 2026-08-29

`verdict=yes`. A delivery path that reserves 128 bytes before it builds the
handler frame keeps the red zone whole, the handler still runs, and the
interrupted computation still finishes intact.

**The controls behave.** The watcher spinning undisturbed over two hundred
thousand passes saw nothing move. A handler frame built at the interrupted `%rsp`
lost the word at offset 8 and on down, on every one of two thousand deliveries --
which is where Cygwin's real path takes it, and what makes a clean reserved run
mean the reservation worked rather than the model going quiet.

**The reservation holds the red zone.** A frame built 128 below the interrupted
`%rsp` wrote nothing inside the 128. The stub still takes its own `%rsp-8`, so the
nearest word it reached was offset 136, one word past the reserved region, on
every delivery; the psABI's 8 through 128 never moved. `redzone_reservation=holds
across delivery`.

**The handler ran, and returned.** Two thousand redirected deliveries each
reached the C handler and came back, counted at both the call and the return, so
the clean reserved measurement is a delivery held off the red zone rather than a
delivery that silently did nothing.

**The computation survived, not just the bytes.** The accumulate watcher folded a
value carried only in its first red-zone word across two thousand reserved
deliveries and more than a million rounds, and finished on exactly the checksum
an undisturbed run reaches. Delivered naively instead -- the negative control the
test harness builds -- the same value breaks, because the frame takes the word
while it is the value's only carrier. So the reservation preserved the
computation and not merely the region.

**Reservation composes, and an alternate stack needs none.** A second delivery
raised while the handler ran reserved its own 128 below the handler's `%rsp` and
left the outer frame's red zone, high above both, untouched -- two handlers per
episode, five hundred episodes, nearest offset still 136. A delivery onto an
alternate stack touched the interrupted stack not at all, nearest offset zero,
and still landed and returned.

So a delivery site can be made to honor the red zone, which is what WP-43 needs
to know before it can carry an ELF-faithful repair and let `-mno-red-zone` come
off. Whether it should is the other half of the choice: this priced the
reservation, not what reserving it costs in Cygwin's real `sigdelayed` per
delivery, and that number is WP-43's to produce. Taken against this transcript
rather than ahead of it, a yes is what a decision record retiring the flag would
rest on; the record is still WP-43's to write.

## What this does not answer

The real `sigdelayed`. This models delivery with a self-driven hijack; it does
not touch Cygwin's own `setup_handler`, its `sigframe`, or the register save area
a real `SA_SIGINFO` frame carries, and the 128 reserved here is a bare gap rather
than a gap around a real frame layout. WP-43 owns that integration and
re-measures it.

The cost. This asks whether the red zone can be reserved, not what reserving it
costs in Cygwin's real path per delivery, which is the other half of the
operator's choice and belongs to WP-43.

Hand-written assembly. A delivery path that reserves the red zone honors it for
hand asm as much as for compiled code, which is the whole point of moving the
repair to the delivery site; but this spike measures the reservation, not any
particular hand-written consumer, so the WP-16 ledger's reason to exist is
narrowed rather than removed.

Unwinding. Spike 3 left `RtlUnwindEx` through a `sysv_abi` frame unmeasured and
this does not reach it either. A reserved red zone and a walkable frame are
different guarantees.

One Windows build, one processor count, one compiler, the pinned 2019 root, as
with every spike here. Spike 1 decided a layer off one machine, so the narrowness
is worth stating rather than assuming away.

## Not verified

That the transcript regenerates. It was regenerated more than once on 2026-08-29
with matching verdicts and matching red-zone findings, but the word counts and
fold totals move between runs by design, so the transcript reproduces in its
findings rather than byte for byte. Diff it on the summary block.

That the model matches Cygwin's real `sigdelayed`. `deliver-naive` reproduces
spike 3's finding -- the red zone lost from offset 8, on every delivery -- which
is the evidence that this model destroys the red zone where the real path does.
It is not the real path, and the claim that a reserved delivery in `sigdelayed`
itself would behave as `deliver-reserved` does here is the hypothesis WP-43
re-measures, not a finding.

That offset 136 is a property of the repair rather than of this stub. The
reservation is 128; the stub then takes its own `%rsp-8` before it builds
anything, so 136 is 128 plus that first word. A real delivery reserves the red
zone around a frame with a shape, and where its first write lands depends on that
shape. What is measured here is that nothing fell inside 128, not that 136 is the
number a real path would report.

That the driver's aim at the integrity window is neutral. `resume-integrity`
lands its deliveries in the window where the accumulate watcher's value lives only
in the red zone, which is the worst case and the relevant one; it is not a
uniform sample of arrival times, and does not claim to be.
