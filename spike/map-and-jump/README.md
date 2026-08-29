# Map and jump

Can a PE stub map a static ELF image and transfer control to it, with no
dynamic linking in play? Yes, and there is a constraint on where.
`results-2026-08-29.txt` is the transcript and the reading is below it.

Three scripts take the measurement. `make-elf.py` synthesizes the specimen,
`payload.S` is the code inside it, and `stub.c` with `enter.S` is the stub
that maps it; `map-and-jump.sh` builds all of that, runs six cases, and writes
the transcript. What is kept here is the means of taking the measurement
again.

**Gates.** The image mapping and initial process image layers, WP-32 and
WP-40, and through them the stub in WP-41.

## Running it

    ./map-and-jump.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted. Every case builds its own
specimen, maps it inside the stub's own process, and frees it again; the
working directory is temporary unless `--keep` says otherwise. A full run is a
few seconds.

`--terse` prints the summary block alone, one `key=value` per line, which is
the form to quote in a document. `--case NAME` runs one case. `--repeat N`
sets how many times a mapping case that failed is asked again, which is how
the transcript separates a standing obstacle from a stray allocation.

## The specimen, and why it is built by hand

The spike needs a static ELF and this machine has no toolchain that emits one.
Cygwin's binutils targets PE; nothing here cross-compiles to Linux; and
borrowing a vendor binary would bring a dynamic loader's worth of assumptions
with it. So `make-elf.py` writes the ELF header and program headers directly
and `payload.S` supplies the text, assembled by Cygwin's `as` into a COFF
object and flattened with `objcopy -O binary`. There is no linker in the
loop, so the payload may not name an address: everything it touches arrives
either on the stack the stub built or through `%rip`.

The geometry is therefore a choice rather than an observation, and the choices
are the ones a static binary linked by a modern `ld` would have. Three
`PT_LOAD`s -- read and execute, read only, read and write -- because that is
the standard partition. Segment addresses congruent to their file offsets
modulo `p_align`, because that is what the format requires and the arithmetic
the stub has to get right. A `p_memsz` larger than `p_filesz` on the writable
segment, because `.bss` is where a loader that forgets to zero gets caught. A
`PT_GNU_STACK`, because every el8 binary carries one.

`EI_OSABI` stays `ELFOSABI_NONE`. WP-10 decides what that byte becomes and
nothing here is entitled to guess ahead of it.

## What the stub does, and what it reports

It reads the file, checks it is `ET_EXEC` and x86-64, and takes the span from
the `PT_LOAD` set. Then, before printing a single line, it surveys the range,
reserves it, and records the result. Only afterwards does it produce output.
That ordering is not stylistic. `printf` allocates, Windows satisfies an
allocation with no requested base out of the lowest free hole, and in a
process whose own image sits high that hole is exactly the one the image
wants; the first version of this printed first and reported a 1.3 MB mapping
at an address that had been free a moment earlier. A measurement the act of
measuring perturbs is worse than none.

With the span reserved it commits each `PT_LOAD` writable, copies `p_filesz`
bytes in, and only then applies the protections, in two passes rather than
one, because two segments can share a page and a segment protected read-only
before its neighbour is copied would make the copy fault.

Then it builds the stack the psABI describes -- `argc`, `argv`, its
terminator, an empty `envp`, an auxv -- and enters `e_entry` by `jmp` rather
than `call`, because `argc` belongs at `(%rsp)` and a call would put a return
address there. Three spike-local auxv keys carry the addresses the image needs
to report through. The image leaves by restoring a stack pointer the
trampoline parked for it, since there is no `exit` to call: the down-call
surface is WP-41 and this spike predates it.

What it checks, per case: that the protections Windows reports back are the
ones asked for, that a store into the text segment faults and a call into the
data segment faults, that the image ran, that it read its read-only segment,
that the word past `p_filesz` was zero, that it found `argc` and `argv[0]` and
walked `envp` into auxv, that it ran at its link address, and that every
register the Microsoft ABI calls callee-saved came back intact.

The fault probes are separate from the `VirtualQuery` check on purpose.
`VirtualQuery` reports what Windows wrote down about a page; the probes ask
the page. A spike that only reads back its own request has measured its own
request.

## The cases

Four ask to be mapped and run. Two are controls that have to be turned away,
and they are the point of the shape: a spike that runs only the cases it
expects to pass has measured its own optimism.

| case | base | `p_align` | asks for |
|---|---|---|---|
| `flat` | `0x400000` | `0x1000` | map and run |
| `huge` | `0x400000` | `0x200000` | map and run |
| `hugehigh` | `0x10000000` | `0x200000` | map and run |
| `offgranule` | `0x8048000` | `0x1000` | map and run |
| `occupied` | the stub's own image base | `0x1000` | refusal |
| `nowhere` | `0x800000000000` | `0x1000` | refusal |

`huge` and `hugehigh` are the same geometry at two addresses, and the pair is
deliberate. On its own, a failure at `huge` cannot say whether the stub got
the 2 MB arithmetic wrong or the address was unavailable. The pair answers
both; either alone answers neither.

`occupied` reads the stub's module base at run time rather than carrying it as
a constant, because a rebuild can move it and a hardcoded control would
quietly stop being one.

## The verdict, 2026-08-29

`verdict=yes`. Five of six cases came out as asked, and the sixth is the
finding.

**The mapping and the jump work.** `flat`, `hugehigh` and `offgranule` each
reserved their span, took their protections, ran, and returned, with every
check passing. The image read its read-only segment, found `argc` and
`argv[0]` on the stack it was handed, walked `envp` into auxv and read
`AT_PAGESZ` out of it, and reported an `%rip` inside its own text segment. So
a static ELF entered at its link address, with the stack the psABI describes,
under a PE process, is a thing that happens.

**Protections are exact, and they are real.** Every segment came back from
`VirtualQuery` as the protection asked for -- `EXECUTE_READ`, `READONLY`,
`READWRITE` -- and both fault probes fired: a store into the text segment
faulted and a call into the data segment faulted. `W^X` and NX hold without
being asked for.

**`.bss` costs nothing.** The word past `p_filesz` read as zero and the stub
never zeroed it. Windows hands back freshly committed pages already zeroed, so
the `p_memsz` tail is free. WP-32 can drop that line item.

**A non-granule-aligned link base costs address space, not correctness.**
`offgranule` links at `0x8048000`, which is 32 KB past a 64 KB boundary, so
the reservation has to start at `0x8040000` and 32768 bytes below the image
are spent on nothing. The case ran. The 64 KB granularity is a tax on the
reservation, not an obstacle to it.

**The constraint: at 2 MB alignment the low addresses are already gone.**
`huge` failed, and it failed the same way twenty times out of twenty. A
`p_align` of `0x200000` puts each of the three segments on its own 2 MB
boundary, which inflates the span from `0x6080` to `0x404080` -- four
megabytes of address space to carry eight kilobytes of content. At
`0x400000` that span is not free. Measured across runs, the free run starting
there is between `0x200000` and `0x260000`, never the `0x405000` wanted, and
what stands above it is a one-megabyte private reservation with its first
64 KB committed, plus a mapped region above that. Both were placed before
`main` ran and neither is ours.

The same geometry at `0x10000000` mapped and ran with `0x6ffe0000` free where
it landed. So the obstacle is the address, not the arithmetic, and the
arithmetic is fine.

**What put them there is the bottom-up allocator, and the spike caught it
doing so.** In the `hugehigh` case the stub's own stack allocation, requested
with no base, came back at `0x400000` -- the very hole the low cases want.
Windows satisfies a based-anywhere request from the lowest free region large
enough, so anything in the process that allocates before the image's span is
claimed can take part of it, and a Cygwin runtime does allocate before `main`.

The consequence for WP-41 is a constraint on *when*, not on *whether*: a
non-PIE image's span has to be reserved before anything else in the process
allocates without a base. A stub that reaches `main` under a warmed-up
runtime has already lost the low addresses. Reserving from a PE TLS callback
or the image entry point, ahead of the runtime's own initialization, is where
this points; nothing here has measured whether that is early enough, and it
should be measured before WP-41 is written rather than discovered inside it.

**The ABI crossing held, in its easy form.** The trampoline returned all eight
callee-saved GPRs and all ten callee-saved XMM registers intact, against an
image that deliberately poisons every one of them on its way out. That is not
spike 3's question -- no signal, no MS-ABI code called from the ELF side, no
unwind data crossing -- and it should not be read as an early answer to it.
It is the minimum a stub needs to survive a call, and it costs one save and
one restore in `enter.S`.

## What this does not answer

Everything the question excluded, which is most of a loader. No dynamic
linking, so no `DT_NEEDED` walk, no relocation, no symbol lookup and no
version matching. No `execve` dispatch, no `fork`, no signals. No bounds
checking worth the name either: `stub.c` parses a file it generated itself,
and parsing attacker-shaped input is WP-31, which gets fuzzed. Do not lift
this file into the tree; lift the findings.

The auxv here carries four entries and three of them are spike-local. WP-40
owns the real one, and the comparison against what a Linux kernel builds is
WP-T2's.

## The tests

`t/run-tests.sh` checks what the two commands refuse, that the generator is
deterministic, and one thing the spike's own output cannot show.

That last one is the reason the file exists. `abi_probe` reporting zero means
either the trampoline saved everything or the check is broken, and from
outside those look identical. So the stub is built a second time with
`-DSPIKE_NO_SAVE`, which leaves the saving out of `enter.S` and nothing else,
and the check has to report `0xff` and `0x3ff` against it. It was watched
failing before it was believed passing. Twenty-five checks, all green on
2026-08-29.

## Not verified

That el8 static binaries carry a `p_align` of `0x200000`. The `huge` case is
built on `ld`'s documented default rather than on a vendor binary, and
`IMPLEMENTATION-PLAN.md` already lists the one `readelf` that would settle it
as unrun. If they do not, the constraint above is narrower than it looks; if
they do, it is the ordinary case.

That reserving from a PE TLS callback or an image entry point runs early
enough to claim the low span. It is where the finding points and nobody has
measured it.

What the two allocations standing at `0x600000` and above actually are. The
survey reports their size, state and type, and they carry no module name, so
they are not images. Naming them would be a guess.

That the geometry the specimen carries is the geometry a real static el8
binary carries. Three `PT_LOAD`s in that order is the standard partition and
the congruence rule is the format's, but a vendor binary also carries section
headers, a `PT_NOTE`, and hundreds of kilobytes of text, none of which is
here. Nothing in the stub's arithmetic depends on those, which is the reason
the substitution was thought acceptable rather than a proof that it is.

That the transcript regenerates. It was regenerated twice on 2026-08-29 with
matching verdicts, but the addresses in the survey block move between runs by
design, so the transcript is reproducible in its findings rather than byte for
byte. WP-T3's runner will need to diff it on the summary rather than on the
whole file.
