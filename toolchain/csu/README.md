# Startup files and the sysroot

WP-14. Four objects, a directory tree, and one exit criterion that is not met.

    build-csu -P <prefix>
    t/run-tests.sh -P <prefix>

`crt1.o`, `Scrt1.o`, `crti.o` and `crtn.o` assemble against WP-12's binutils.
`build-csu` lays out the sysroot with el8's usrmerge links, so `/lib64`
resolves before anything looks for the loader `doc/target-definition.md`
names. The header set WP-50 delivers is installed alongside, reseeded on every
run rather than added to.

`libgcc.a` comes out of WP-13's build rather than this one, because it is
built from gcc's own source tree and configuring it separately would mean
configuring gcc twice.

## What the criterion asks, and where it stands

"A static hello links and the spike 2 stub runs it." The first half holds. The
second does not, and the obstacle is in the stub rather than in anything here.

Seven claims pass: the image links, it is `ET_EXEC` with no interpreter, its
entry is `_start`, its `EI_OSABI` matches the record, a segment carries `.bss`
to zero, and the stack is not executable.

Two fail. The stub maps every segment correctly — its own verbose output shows
four `PT_LOAD`s committed at the right addresses with the right protections,
and its store-into-text and call-into-data probes both fault as they should —
and then the image never comes back.

## Why, and it is worth reading

Spike 2 could not link anything. Its README says so: this machine had no
toolchain that emitted a static ELF, so `make-elf.py` wrote the ELF and program
headers by hand and `payload.S` could not name an address, because no linker
was in the loop. Everything the stub knows about image shape it learned from
that synthesized specimen.

A real linked image is not that shape. `ld` gives this hello four `PT_LOAD`
segments — a read-only one for `.note.gnu.property`, an executable one, a
second read-only one for `.eh_frame`, and a writable one — where the specimen
had exactly three. The stub refuses an image that is not one executable, one
read-only and one writable segment; a program with no writable data at all is
rejected outright with `this is the specimen shape`.

So the first thing WP-12 made possible has immediately found a limit in the
harness that was written before it existed, which is roughly what should
happen. `issue/0001-the-stub-assumes-a-synthesized-shape.md` states it as work.

Nothing here is blocked on that. The startup files are correct as far as
anything can currently check, and `t/exit-spike2.S` disassembles to exactly
the auxv walk it was written as.

## _exit, and why it is in the test directory

`crt1.o` calls `_exit` and does not care who supplies it. On a finished
platform that is the runtime reached through the veneer, and WP-41 is what
gives a process somewhere to go when it ends.

The one under `t/` returns to spike 2's stub instead, by the protocol that
spike invented: the stub parks its own stack pointer in a handshake block,
passes the block's address in an auxv entry of its own, and gets control back
when the image restores that pointer and returns. That is a spike's shape and
not a platform's, which is why it sits in the test directory where it cannot
be linked into anything shipped.

## Not verified

Whether the image runs. That is the point of the two failing claims and it
stays open until the stub can take a linked image.

`Scrt1.o`. It assembles and nothing has linked a PIE with it, because a PIE
needs a dynamic loader and that is phase 3.

The `.init` and `.fini` pairing. `crti.o` and `crtn.o` are a matched pair and
a link that includes one without the other produces a function with a prologue
and no return. Nothing checks the ordering yet; the compiler driver gets it
right and a hand-written link can get it wrong.

That `-Ttext-segment=0x10000000` is the right base. It is spike 2's, chosen
because that spike measured a 4 MB span at `0x400000` refused twenty times in
twenty inside a Cygwin process. WP-41 owns the real answer.
