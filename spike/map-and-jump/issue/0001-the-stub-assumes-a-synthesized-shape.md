# Issue 0001 — the stub only accepts the shape it synthesized

Status: open
Raised: 2026-08-29, by WP-14
Against: `stub.c`, `map-and-jump-stub 1.0`
Blocks: WP-14's exit criterion

The spike's verdict stands and nothing here disputes it. A PE stub can map a
static ELF and enter it; the transcript is real and the mechanism works.

What has changed is that the spike's own precondition has expired. Its README
records the constraint plainly: this machine had no toolchain that emitted a
static ELF, so `make-elf.py` wrote the headers by hand and `payload.S` could
not name an address because no linker was in the loop. WP-12 delivered a
binutils that emits ELF for the triple, and WP-14 linked the first real image.
The stub will not take it.

## What happens

The stub maps it correctly. Its verbose output shows four `PT_LOAD` segments
reserved and committed at the right addresses with the right protections, the
store-into-text probe faulting and the call-into-data probe faulting, exactly
as it does for the specimen. Then control transfers and does not come back.

A smaller image, one whose `main` returns immediately and which therefore has
no writable data, is refused before mapping:

    map-and-jump-stub: tiny.elf wants one executable, one read-only and one
    writable PT_LOAD; this is the specimen shape

## Why

`make-elf.py` produces exactly three `PT_LOAD` segments, one of each
protection, and the stub was written against that. A real linked image is not
shaped that way. `ld` gave WP-14's hello four: a read-only segment for
`.note.gnu.property`, an executable one, a second read-only one for
`.eh_frame`, and a writable one. Two read-only segments is ordinary output and
there is no reason to expect one.

The handshake block is the other half of the same assumption. The stub places
it at the head of the writable segment because that is where `make-elf.py` put
it. In a linked image the head of the writable segment is whatever `ld` put
there, which for WP-14's hello is `data_word`. So the stub writes its magic and
its parked stack pointer over the image's own data, and the image reads its
data out of the stub's handshake fields. Neither notices.

That is the likelier cause of the hang than anything in the startup files.
`t/exit-spike2.S` disassembles to exactly the auxv walk it was written as, and
it spins by design when it cannot find `AT_SPIKE_HANDSHAKE` rather than
faulting, so a hang is what a lost handshake looks like from outside.

## What would close it

Take the image's shape from the image. Walk every `PT_LOAD` rather than
matching three, which the mapping half of the stub already does; the
restriction lives only in the acceptance check.

Put the handshake somewhere the image agrees about. An auxv entry already
carries its address, so it does not have to be inside the image at all — a
page the stub allocates for the purpose would be found by exactly the same
walk and would stop the two sides sharing storage. That is a smaller change
than it sounds and it removes the collision permanently.

Neither is a change to what the spike measured. The transcript stays valid;
what wants regenerating afterwards is the acceptance criterion, not the
verdict.

## Not verified

That the shape check is the whole cause of the hang. The overlap between the
handshake block and the image's `.data` is established by reading both, and
nobody has instrumented a run to watch it happen. Fixing the placement is
cheaper than proving it is the cause.

Whether WP-32 inherits the same assumption. The stub is spike code and the
loader is not, but the loader is written from the same reading, and "one of
each protection" is exactly the kind of thing that gets carried forward
without being noticed.
