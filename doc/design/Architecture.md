# Architecture

What this system is, in the present tense. No history lives in the body: a
reader who wants to know why a thing is the way it is, or what it was before,
goes to `doc/design/decisions/`, and a reader who wants to know what is built
goes to `doc/status/delivered.txt`. This document is the design of record.
`doc/history/elf-technical-breakdown.md` is the founding survey it grew out of
and is kept as history rather than as a plan.

A section that a decision record settled ends in one line naming the records.
A section with no such line describes behaviour inherited from Cygwin that no
record has touched: that is the ordinary case for a re-facing project and it
is not a gap. An unlined section is never an unsettled one.

The bar is that this document is enough to build from. A reader implementing a
layer here should not have to open a decision record to find a value, a
constant, an address, a limit or a rule; the records carry the argument and the
alternatives that lost, and those are a different question from what to write.
So a section states its numbers and its invariants outright, and where it names
a measurement it names the figure rather than the fact that one exists.

A claim about the present state is checked against the tree, never against the
record that decided it. The two diverge in one direction and it is always the
same one: a record states where a thing belongs, the work to put it there is
scheduled, and the record reads as though it had happened. Where a requirement
is settled but not yet enforced where it should be, the section says what
enforces it today and what is outstanding, in the same breath. A governing
document that reports a destination as an arrival is the failure the whole
governed set exists to prevent, and it is easier to commit here than anywhere
else in the tree.

The section list here is the taxonomy the rest of the governed set follows.
Length is not a reason to split a section out; incoherence is. Depth belongs
here by default, and a sub-document exists only where a subject is large and
self-contained enough that a reader of it is not reading this document at the
time. Six exist: `doc/design/ABI-Boundary.md` for the seam's per-symbol rules,
`doc/design/glibc-reuse.md` for what of glibc may be taken and on what grounds,
`doc/design/Symbol-Resolution.md` for the lookup engine and the version
matcher, `doc/design/Address-Space.md` for the low window and protection
precision, `doc/design/Runtime-Crossing.md` for how a process comes to host the
faced runtime, and `doc/design/target-definition.md` for the measurement behind
each of the six target values. All are governed, all are cited from the section
that owns them, and all carry the same Settled-by lines. None is a summary. A
section that hands off keeps its own values and invariants here, so that this
document reads end to end without them.

## Floor and derivation

### What ships

Three artifacts ship, and everything below is about how they are derived:

| Artifact | Installed as | What it is |
|---|---|---|
| `elfsysv1.dll` | the runtime, beside the toolchain | Cygwin's `winsup/cygwin` compiled unchanged, wearing a System V export face |
| `libc.so.6` | `/usr/lib64/libc.so.6` | the veneer: glibc 2.28's versioned ABI, bodies answered from the runtime |
| `ld-linux-x86-64.so.2` | `/usr/lib64/ld-linux-x86-64.so.2` | the loader's ELF-shaped face; its body is in `elfsysv1.dll` |

The platform is Cygwin, re-faced. ELF and the System V AMD64 ABI face outward;
Windows NT is reached inward; and the mechanism is a new export face on the one
DLL every Cygwin host call already funnels through, not a new POSIX
implementation behind it. `elfsysv1.dll`'s body is `winsup/cygwin`'s source at
the ref below, compiled unchanged; its entry points — `DllMain`, thread entry,
the signal path — are re-faced rather than reimplemented. Writing a fresh Cygwin
would violate this as surely as re-authoring a vendor header would.

The source tree divides on that line. `runtime/` is the re-facing of the body:
`winsup` for the source, `exports` and `imports` for the two generated
inventories, `face` for the System V faces over Microsoft bodies, and `varargs`,
`signal`, `tls`, `core`, `coredump` and `version` for the paths that need more
than a face. `loader/` is written work with no Cygwin behind it —
`elf`, `map`, `graph`, `reloc`, `lookup`, `version`, `tls`, `dl`, `rdebug`,
`process`, `exec`, `fork`. `veneer/` is the glibc-ABI face: `include` for el8's
vendored headers, `libc` for the classification tables, `classification`,
`wiring`, `version-map`, `companions` and `xlat` for what turns a name into a
body.

Neither inventory is written by hand. The outward export list is cut from
Cygwin's own `cygwin.din` into `runtime/exports/cygwin-exports.tsv`, and the
inward import list from the built DLL's import table into
`runtime/imports/cygwin-imports.tsv`; the down-call wrappers and the veneer's
version map are generated from those two. Both are one version's lists, which
is why the ref is named rather than implied.

### Why the C library is a face rather than glibc

The veneer is a face over newlib plus Cygwin. It is not el8's glibc, and the
reason is a measurement rather than a licence. `doc/design/glibc-reuse.md`
carries the argument in full, because three separate questions are collapsed
into this one often enough that they need somewhere to be kept apart.

Licensing does not rule glibc out and never did. glibc is LGPL-2.1-or-later,
which this tree's LGPLv3-or-later absorbs outright under LGPL-2.1's own section
13, and glibc material is in fact taken here: the headers under
`veneer/include/` are el8's own, vendored byte-identical. What rules glibc code
out where it is ruled out is coupling — its resolver assumes `_rtld_global`,
its own `link_map`, and being the process's first mover — and coupling is a
different claim from licence, which is why the governing document is required
to say which one it means.

What rules out running glibc itself is that its compiled code cannot execute on
this host.

glibc's compiled code reaches every thread through `%fs`. The thread pointer
sits at `%fs:0`; initial-exec and local-exec TLS are literal `%fs`-relative
instructions in the object code; `errno` and the locale arrive the same way.
Spike 1 measured a user-written `%fs` base failing to survive a context switch
on this Windows, and failing inside the scheduler no matter who set it. Those
are hardware segment accesses compiled into shipped objects, not system calls a
personality layer could intercept, so no depth of Linux-syscall emulation
underneath makes them work. Running el8's glibc unmodified over a kernel
personality is not a harder version of this project. On this host it is a
different and impossible one.

### The copy line

That is why the substitution happens at the floor rather than above it. glibc
is not userland; it is what userland stands on. The platform fills glibc's role
ABI-identically, and because it does, everything above stays unchanged.

The copy line is the practical form of that. el8's packages build pristine, and
the headers they compile against are el8's `glibc-headers-2.28`, vendored
byte-identical under `veneer/include/` with `gnu/stubs.h` the one justified
exception. Copying rather than paraphrasing is what commits the runtime to
presenting Linux's ABI exactly — errno values, signal numbers, struct layouts —
and translating up from Cygwin inside the face, which is where the real work of
this project lives. A varied file above the floor is an exception carrying a
written reason: a literal host-triple test, or hand-written assembly that
assumes a red zone. The burden of justification falls on the variance, never on
the copy.

### Two Cygwin versions, and what may build the runtime

Two Cygwin versions are in play with different jobs, and conflating them is the
mistake this subsection exists to prevent. One is what the runtime is *made
of*; the other is what a built result is *checked against*. Where each
installation lives is a property of a machine and belongs in that machine's
working notes, not here.

The runtime is built from Cygwin 3.6.10, `newlib-cygwin` at commit
`b11613e47`, and every artifact that reads Cygwin's source for the runtime's
shape takes it from that ref: the export inventory, the down-call wrappers, the
veneer's version map. The verification target is Cygwin 3.0.7, which is the
RHEL-8.10 emulation, and it holds no build or certification role at all.

The build root is therefore constrained from below. It has to be able to
compile `winsup` at 3.6.10 and the certifications beside it. gcc 14.4, python
3.12 and make 4.4 are the versions this is done with and are known to work;
gcc 7.4 is known not to, which is what retired the 3.0.7 installation from the
build role. The exact floor between those two has not been measured, so a
project standing this up elsewhere should read 14.4 as sufficient and 7.4 as
insufficient rather than as a stated minimum.

One rule follows from having two installations at all, whatever their paths. A
binary resolves `cygwin1.dll` off the invoking shell's `PATH`, and the failure
when it resolves the wrong one is a hang rather than an error, so it reads as a
slow tool instead of a mistake. Never run one root's binaries from another
root's shell.

### The compatibility counter

Binary compatibility runs on three inherited axes rather than invented ones,
declared in `runtime/version/elfsysv-version.h`:

    generation   the digit in the name elfsysv1, re-facing Cygwin's
                 CYGWIN_VERSION_DLL_IDENTIFIER "cygwin1". No counter reaches
                 it; a different digit is a different DLL a program never
                 loads by accident. Starts at 1, moves only for a break no
                 backward-compatible counter could bridge.
    api major    re-facing CYGWIN_VERSION_API_MAJOR. Reserved for an
                 incompatible change within a generation.
    api minor    re-facing CYGWIN_VERSION_API_MINOR. Bumped additively for
                 every export a program could depend on.

The pair starts at `0.1`, and `runtime/version/CHANGELOG.md` records what each
bump means; a counter without a changelog is a version nobody can reason about.
Cygwin keeps the same list inline in `winsup/cygwin/include/cygwin/version.h`,
where at `b11613e47` its minor runs to 357.

A program carries a stamp of what it was built against, `elfsysv_version_stamp`,
re-facing the `api_major`, `api_minor` and `magic_biscuit` fields Cygwin's crt0
writes into `per_process`. The runtime reads it at load and refuses, with a
diagnostic rather than a crash, when the program asks for more than the runtime
provides. That check re-faces Cygwin's `check_sanity_and_sync` in `dcrt0.cc`.

The refusal reads the combined `major * 1000 + minor` and not the major alone,
which is the deliberate departure from Cygwin, whose load-time test is
`p->api_major > cygwin_version.api_major` with the minor left to compile-time
feature macros. Every additive change to this project's exported surface lands
in the minor, so a program built after one genuinely needs a runtime carrying
it; testing the major alone would admit a program built against `0.7` to a
`0.5` runtime and fail it at the first missing export instead of at the door.
The major axis is not weakened, because the combined number carries it: `1.0` is
`1000` and outranks every `0.x`.

The counter starts at `0.1` at the first release rather than retrospectively.
Retrofitting one means guessing which already-shipped binaries predate which
change, and the binaries do not carry the answer, so the guess cannot be made
honestly. The consequence for a user is Cygwin's own: compatibility is
backward only. A program built against a higher combined value does not run on
a lower runtime, which is why borrowing a binary from a newer tree is never an
option and building from source is the only route.

Settled by: DR-0000, DR-0007, DR-0018.

## Target and claim: x86_64-elfsysvnt-linux-gnu

### The six values

Six values have to agree, because each is compiled into a shipped artifact and
changing one after packages exist means rebuilding them. They are set here and
`doc/design/target-definition.md` carries the measurement behind each.

| Value | Setting |
|---|---|
| Target triple | `x86_64-elfsysvnt-linux-gnu` |
| `EI_OSABI` | `ELFOSABI_NONE`, promoted to `ELFOSABI_GNU` on `STT_GNU_IFUNC` or `STB_GNU_UNIQUE` |
| `.note.ABI-tag` | owner `GNU`, type `NT_GNU_ABI_TAG`, 16-byte payload: OS 0 (Linux), then 3, 2, 0 |
| Loader SONAME | `ld-linux-x86-64.so.2`, installed at `/usr/lib64`, with `/lib64` a symlink to `usr/lib64` |
| `uname` | `Linux` / `4.18.0-elfsysvnt` / `#1 SMP elfsysvnt` / `x86_64` |
| PIE default | the compiler defaults to `-pie`; executables are `ET_DYN` |

The spellings derived from the triple are what a later package hardcodes, so
there is one of each and no shortened alias — a second spelling for one target
is how a build ends up half cross-compiled:

    sysroot          /usr/x86_64-elfsysvnt-linux-gnu/sys-root
    tool prefix      x86_64-elfsysvnt-linux-gnu-
    gcc --target     x86_64-elfsysvnt-linux-gnu
    rpm %{_target}   x86_64-elfsysvnt-linux-gnu

### The fields, and the vendor slot

The fields are `cpu-vendor-kernel-os` in `config.sub`'s own naming, which sets
`kernel=linux` and `os=gnu` and validates the pair under `case $kernel-$os-$obj`.
Nothing in a four-field triple is called `abi`. The project's name occupies the
vendor slot because that is the one slot nothing reads: `config.sub` passes an
unrecognized vendor through untouched. Putting the name in `os` instead is the
trap worth knowing, because it does not fail — `config.sub` accepts `elfsysvnt`
there by matching `elf*`, the entry that exists for bare-metal targets of the
`i386-elf` kind, and `config.gcc` then reads `x86_64-*-elf*` as bare metal and
routes the build to a target definition with no operating system beneath it.

### What each load-bearing field claims

`gnu` is true with nothing subtracted: everything shipped is ELF, System V,
versioned, and reaches `libc.so.6` through `ld-linux-x86-64.so.2`.

`linux` claims the Linux interface as a rebuilt el8 package consumes it —
through `libc.so.6`, through `/proc`, through the auxiliary vector a process is
entered with, and through the `uname` strings above. It does not claim the
system-call interface, and the distinction is sharper than "one item
undelivered" suggests. Nothing here dispatches a system call or implements a
syscall number. A package's `open` reaches the runtime's `open`, which reaches
NT; no number is involved anywhere on that path, so syscall numbers are not
something this platform gets wrong, they are something it does not have.

Two consequences follow, and only the first is recorded as the bound. An object
reaching the kernel through a raw `syscall` instruction is outside the contract
the triple advertises, because each package is rebuilt against `elfsysv1.dll`
rather than having anything intercepted; no field of any triple expresses that
restriction, which is why it is prose here. The second is that `syscall` the
libc function is equally unavailable and is *not* covered by that bound: it is
a public glibc export, a great many real packages call it directly, and its
disposition here is a bucket-4 stub. A rebuilt package calling it has used no
raw instruction and is still stopped. How many of the el8 set that reaches is
uncounted.

The vendor binaries this platform exists to run were compiled under the
unbounded claim, so their raw syscalls sit exactly on the axis where this one
stops.

### Where the name lives

Two of those six values are where the honest name can sit at all, and the
reasoning is worth carrying because it constrains anyone tempted to move it.
`EI_OSABI`, the ABI-tag note, the loader SONAME and both load-bearing triple
fields are read by consumers with a definite expectation, and in each of them a
truthful string costs a broken check or a broken build: our loader must accept
vendor objects, a real `ld.so` may one day read ours, and every el8 binary in
existence names `/lib64/ld-linux-x86-64.so.2` in its `PT_INTERP`. So the name
lives in the vendor field and in `uname -r`, where `4.18.0-elfsysvnt` compares
equal to el8's `4.18.0` for every parser that stops at the first non-digit
while a human running `uname -r` sees what they are on. It also gets a note of
its own — owner `ELFSYSVNT`, type 1, section `.note.elfsysvnt.abi`, payload two
32-bit words carrying the API major and minor — which costs thirty-two bytes
per object and no compatibility surface, since nothing reads an unknown note
owner.

### When the triple reopens

The triple is one of the three decisions reserved to the operator, and it
reopens against a measured share rather than an appetite. Spike 5 counts the
packages whose vendored `config.sub` matches a literal `*-pc-linux-gnu` or
`*-unknown-linux-gnu` and therefore misses silently under any other vendor. Of
2893 source names in Rocky 8.10, 891 carry a `config.sub` and 32 are affected
either way, an `affected_share` of 1.1% measured 2026-08-31. Under 2% is a
patch set and the decision stands; 2% to 10%, roughly 58 to 289 packages, means
it stands with a named burden in the refresh policy; over 10% means reopening
and masquerading as `x86_64-pc-linux-gnu`, with the honest name moving to
`EI_OSABI`, the note, the SONAME and `uname`. That last is a second program of
work and belongs to whoever holds the budget.

### Where el8 source comes from

el8 source is Rocky Linux 8.10's four trees — `BaseOS`, `AppStream`,
`PowerTools`, `extras` — from `dl.rockylinux.org`, held outside the repository
and vendored nowhere. Nothing in this repository depends on it being present,
and where a machine keeps it is that machine's business. The
tree is not frozen upstream, so every run records the `repodata/repomd.xml` of
each repository it read into its manifest, and that snapshot is the pin without
which a transcript cannot be reproduced. AlmaLinux 8.10 is the cross-check for
specs and downstream patches; it holds no upstream tarballs, so it is the wrong
instrument for anything that reads inside one.

Settled by: DR-0001, DR-0002, DR-0005.

## Toolchain and images

### Four defaults, and where each is enforced

The cross toolchain builds for the triple in § Target and claim, and is
installed root-neutrally so that it resolves the same from either Cygwin shell
on a machine that has both. Four properties are required of every object the
platform loads. A
mandate belongs where a later flag cannot silently drop it, so the destination
for all four is the compiler's own default; two are there and two are not yet,
and the difference is stated rather than smoothed over.

| Property | Required setting | Enforced today by | Destination |
|---|---|---|---|
| Red zone | honored; `-mno-red-zone` selectable, never forced | `toolchain/gcc/patches/0001-add-the-elfsysvnt-target.patch`, which no longer sets `MASK_NO_RED_ZONE` | done |
| max-page-size | `0x10000` or larger | `toolchain/gcc/default.specs` | done |
| CET | `-fcf-protection=none` | `%_elfsysvnt_no_cet` in `toolchain/rpm/macros.elfsysvnt`, plus `loader/exec/stub.c`'s refusal | `toolchain/gcc/default.specs`, outstanding at WP-13 |
| PIE | `-pie`, output `ET_DYN` | `%build_ldflags` in `toolchain/rpm/macros.elfsysvnt` | `toolchain/gcc/default.specs`, outstanding at WP-13 |

The two outstanding rows are the reason the column exists. An object compiled
by the target gcc outside an rpm build gets neither the CET opt-out nor `-pie`
today, which is why every `veneer/wiring/t/live-*.sh` passes
`-fcf-protection=none` by hand. A flag that every script repeats is the tell
that an obligation is known and applied ad hoc rather than defaulted where the
toolchain would guarantee it, and the asymmetry with max-page-size — trusted to
the default, no more required — is the evidence. The rpm macros stay as the
package-build layer and the loader stays as the backstop beneath; what is
missing is the default under both.

Both rows are scheduled work rather than open questions, decided on
2026-09-03: they move into `toolchain/gcc/default.specs` beside max-page-size,
and the rebuild that makes a new default take effect is what gates them.
`default.specs` has carried the one link line and nothing else since it was
created, so this is a consequence never carried out rather than a setting that
regressed.

### The red zone

The red zone is honored, and the flag that used to say otherwise is gone. The
psABI reserves the 128 bytes below `%rsp` for a conforming leaf, the host
respects them — spike 3 measured Windows leaving them alone under preemption,
thread hijacking and its own exception dispatch — and the one violator was
Cygwin's own signal delivery, which took the word at `%rsp-8` on every
delivery. The repair is at the delivery site: delivery reserves the 128 bytes
before it builds the handler frame, and `runtime/signal/t/run.sh` certifies on
the primary root that a signal delivered into a running thread leaves them
intact. The measured cost of that reservation is a median of −0.49% of a
delivery, inside the under-5% band written before the number existed, so
`-mno-red-zone` came off the target default on 2026-08-31. It remains
selectable per compile; it is no longer platform policy, and nothing may assume
a red-zone-free leaf.

DR-0050 is the whole of the red-zone reading, and DR-0006 is superseded by it
rather than standing beside it. That is DR-0006's own instruction: it set the
direction, carried the flag as scaffolding, and wrote a band table against a
cost WP-43 had not yet measured, whose first row reads that under 5% the flag
comes off and the record is superseded by WP-43's. The measurement landed
inside that row, so the supersession is the band table executing as written and
not a later judgement about it.

### Granule-separable linking

Every image the runtime loads is linked granule-separable: no two `PT_LOAD`
segments of unlike protection share a host allocation granule, which is 64 KB
(`0x10000`) on the pinned host. This is a build-time guarantee rather than a
best effort, because the loader maps segments through the runtime's own `mmap`
so that `fork` can replay them, and that path separates protection only at the
granule. Two differently-protected segments inside one granule cannot both be
honored, and coalescing them to the union would surrender the W^X and NX the
second-pass protection exists to hold, so the loader refuses the image with
`elf_map_err_granule`. The toolchain's max-page-size default is what makes the
refusal unreachable in practice; a per-link `-z max-page-size` is the fallback
for a build that overrides the default, not the primary means.

Stock el8 binaries satisfy this by accident of the linker's 2 MB default, which
puts every segment in its own granule with room to spare. Images the project
builds itself do not, and the gap is not hypothetical: bzip2's hand-written
Makefile passes no max-page-size, and its first acceptance run halted before
entry with `PT_LOAD[0]` and `PT_LOAD[1]` sharing one `0x10000` granule. An
assumption that holds for the vendor's binaries and not for the harness's own
is exactly what a toolchain default exists to close.

### CET

No image may expect CET — neither shadow stacks nor indirect-branch tracking —
because the process runs with them opted out. Spike 2 established that the PE
stub has to disable shadow stacks and Control Flow Guard for a self-mapped
image to be entered at all, and `loader/exec/stub.c` refuses to start a process
under enabled user shadow stacks, so a package compiled expecting CET is worse
off than one that never asked. `%build_cflags` carries
`-fcf-protection=none` for package builds through `%_elfsysvnt_no_cet`. The
compiler default is where it belongs and is the outstanding row above.

Settled by: DR-0050, DR-0061, DR-0062.

## Thread pointer and TLS

### Why %gs, and not %fs

The thread pointer is a word this runtime owns, reached through `%gs` rather
than `%fs`. `%fs` is unavailable, not merely unattractive: spike 1 and
`spike/fs-base-fault/` measured a user-written FS base being lost in the
Windows scheduler, no matter who set it, so a `%fs`-relative access reads a
zeroed segment and returns garbage.

The chain is `gs:[NtTib.StackBase]`, then a fixed offset — carrier C3 of
`spike/gs-thread-pointer/`, the shape Cygwin's `_my_tls` already uses. A TLS
access fetches the pointer through that chain and then addresses the block
relative to it. The block keeps its glibc layout: `tcbhead_t` at the thread
pointer, the static block at negative offsets.

### Where the carrier word sits

The carrier word lives at the floor of a stack this runtime owns, not at a
blind offset below whatever `StackBase` reports. A managed thread runs on a
512 KB `mmap`'d stack, fully committed, so `NtTib.StackBase` is the top of the
allocation and the carrier sits sixteen bytes above the floor:

    ELFSYSV_TP_STACK_SIZE    512 * 1024
    ELFSYSV_TP_CARRIER_OFF   ELFSYSV_TP_STACK_SIZE - 16
    carrier address          NtTib.StackBase - ELFSYSV_TP_CARRIER_OFF

That places it beneath Cygwin's own `_cygtls` reservation, which is
`CYGTLS_PADSIZE` = `0x3200` at the top of the stack, and far beneath the
working `%rsp`. The runtime caches `CYGTLS_PADSIZE` from the Cygwin it is
running against and refuses to bring a thread up if the reservation has grown
past the carrier offset, because the two constants come from different trees
and a silent overlap would corrupt whichever wrote last. The remedy the
diagnostic names is to widen the stack or move the carrier, never to shrink the
reservation.

### The static-TLS surplus

The static-TLS surplus is a tunable, `ELF_TLS_SURPLUS_DEFAULT`, defaulting to
1664 bytes, and `elf_tls_state_init` takes an override. That number is glibc's
historical `TLS_STATIC_SURPLUS`, `64 + DL_NNS * 100` at the sixteen namespaces
glibc ships, reused rather than invented so that a program with the same
headroom under glibc has the same headroom here. It is room below the initial
modules so that a module loaded later, compiled for initial-exec, can still
take a static offset in a thread created before it existed. A module reached
through `__tls_get_addr` does not consume it: general-dynamic allocates lazily
per thread on first access and is unbounded by the surplus. The two mechanisms
are separate, and getting the surplus wrong is a run-time failure inside a
library the program did not know it would load.

### The DTV

The DTV is glibc's shape down to the negative indices, because `tcbhead_t.dtv`
at head offset `0x08` is read by code the compiler emits and a differently
shaped vector behind that pointer would need a `__tls_get_addr` other than the
one the psABI names:

    dtv[-1].counter    the vector's length, in module slots
    dtv[0].counter     the generation this thread's vector was reconciled to
    dtv[i]             module i's block, or TLS_DTV_UNALLOCATED

A thread learns its vector is stale by comparing `dtv[0]` against the module
table's generation, which is bumped on every add. That is the whole mechanism
by which a thread created before a `dlopen` resolves the new module: the access
finds a newer generation, grows the vector, allocates the block.

### Provenance

The layout, the DTV and `__tls_get_addr` are written from the generic ABI, the
x86-64 psABI's variant II and Drepper's TLS account, not lifted from glibc's
`dl-tls.c`. The reason is coupling rather than licence: `dl-tls.c` assumes a
Linux loader this platform does not have. What is reproduced is the observable
behaviour, and the differential against a real glibc is what certifies it.

### What an image may not contain

No image the runtime loads may contain a `%fs`-relative thread-pointer access.
This is a requirement on every loadable image, not only a property the runtime
happens to want, and it is enforced at link time by
`toolchain/binutils/patches/0001-bfd-refuse-tls-relocations-that-presume-fs.patch`,
which refuses `R_X86_64_TLSGD`, `TLSLD`, `GOTTPOFF` and `CODE_4_GOTTPOFF`
outright. `TPOFF32`, `TPOFF64`, `DTPMOD64` and `DTPOFF64` stay accepted: they
are values rather than instruction sequences, the linker writes no instruction
bytes for them, and the compiler wants `TPOFF` for sequences of its own.

TLS descriptors are refused on the same patch — `GOTPC32_TLSDESC`,
`CODE_4_GOTPC32_TLSDESC` and `TLSDESC_CALL` — and are not implemented anywhere.
The reason is arithmetic: the `%gs` chain needs three instructions where the
psABI reserves sixteen bytes for two. No object this toolchain produces carries
a descriptor relocation, so a resolver written now would be dead code certified
against nothing. If a future toolchain layer emits descriptors, the resolver
lands beside `__tls_get_addr` and reopening is a record pointing back.

The requirement is not enforced against the toolchain's own output, and this is
a defect rather than a gap. Measured 2026-09-03 against gcc 13.3.0 in the
installed cross toolchain, every TLS model emits `%fs`:

    -O2, and -ftls-model=local-exec     movl %fs:le@tpoff, %eax
    -ftls-model=initial-exec            movq ie@gottpoff(%rip), %rax
                                        movl %fs:(%rax), %eax

A global-dynamic object is refused at link, correctly and loudly. A local-exec
object links clean and is wrong: `__thread int le; return le;` produces a read
of a segment Windows has zeroed. The refusal set covers the four relocations
for which the linker writes instruction bytes and deliberately accepts
`TPOFF32`, `TPOFF64`, `DTPMOD64` and `DTPOFF64` as values rather than
sequences. That is true of the linker and silent about the compiler, which has
already emitted the `%fs` prefix the value is used with.

So there is no `%gs` codegen. Fetching the thread pointer takes three
instructions — read the TEB through `%gs`, take `NtTib.StackBase`, subtract the
carrier offset, load — so a complete local-exec access is four and needs a
scratch register, against the one instruction and no scratch the psABI's
`movl %fs:x@tpoff, %eax` costs. The same three-instruction chain is why the
descriptor contract, which reserves sixteen bytes for two instructions, cannot
hold it. Writing that sequence
is the outstanding half of WP-13, alongside the build-side image scan that
would catch what the link accepts. Until both exist, the requirement holds on
every image and is enforced on one class of them.

`doc/design/proposals/0007-the-veneer-under-a-real-glibc.md` records why this
matters beyond this section: a source port of glibc cannot compile its own
plain C until the sequence exists, and glibc is saturated with `__thread`.

Settled by: DR-0003, DR-0021, DR-0024, DR-0063.

## Loader and placement

The loader is this project's own code, in `loader/`, with no Cygwin behind it.
It never hands an ELF file to the Windows loader. Two subsystems inside it are
large enough to carry their own governed documents —
`doc/design/Symbol-Resolution.md` for the lookup engine and the version
matcher, and `doc/design/Address-Space.md` for the low window, placement and
protection precision — and the invariants they must satisfy are stated in
those. What follows is everything else the loader does, in the order it does
it.

### Classification

One classifier, `binfmt_classify` in
`loader/exec/binfmt.c`, over one read of the leading `BINFMT_HEAD_MAX` = 256
bytes, which is Linux's own `BINPRM_BUF_SIZE`. It returns one of four verdicts:
ELF, a `#!` script, an image the host still owns, or nothing recognized. It
tries them in that order — ELF, then `#!`, then the host's `MZ` — and the first
match wins with the rest not consulted. The spawn path calls the classifier
rather than testing for ELF in front of a `#!` test of its own, because two
places that decide what a file is will eventually disagree.

A chain follows at most `BINFMT_MAX_DEPTH` = 4 interpreter hops, which is
Linux's limit counted the same way. A fifth is refused, and that limit is also
the cycle detector: a script whose interpreter is itself spends its four hops
and is turned away, with no separate cycle detection to get wrong. At each hop
the argument vector is rebuilt the way the kernel rebuilds it — drop the
leading element, then push the interpreter, its single optional argument if the
line carried one, and the path of the file that named the interpreter, in front
of what remains. The interpreter's argument is the whole rest of the line
rather than a further split, with trailing blanks stripped. A `#!` line that
does not end within the first 256 bytes is refused rather than truncated. The
vector is rebuilt whether or not the chain ends in ELF, so a script whose
interpreter turns out to be an ordinary host program is handed back to the host
with the rebuilt vector and the resolved file rather than with the arguments the
caller first supplied.

### The cache

`ldconfig`'s cache is this project's format, not glibc's — neither the old
`ld.so.cache1.0` layout nor the `glibc-ld.so.cache1.1` extension el8 writes.
That is a choice rather than an inability, and the reason is that the cache has
exactly one reader.

It participates in one thing: the object graph's name search, between
`DT_RUNPATH` and the default directories. Nothing else in the tree reads it,
and no external tool reads it either. glibc's on-disk cache is shaped by
history this project does not share — two concatenated layouts kept for
backward compatibility, a flags word encoding ABIs and hardware capabilities
that predate x86-64, and an extension section — so reproducing it byte for byte
would be work in service of a compatibility nobody is asking for. Nor would it
buy a stronger test: the load order is certified against a real `ld.so` on the
cases the search precedence turns on, and getting a real `ld.so` to read an
arbitrary cache file is not something its interface offers, so the cache is
exercised against a known-answer construction either way.

What this does not settle is whether the *shipped platform* ever needs glibc's
format. If some vendor tool reads `/etc/ld.so.cache` directly rather than going
through `ld.so`, the answer is a second writer emitting glibc's layout for that
consumer, beside this one — a new writer for an external reader, not a change
to how the loader searches.

It is a header, an entry array and a string table:

    magic     "elfsysv-ldcache"    15 chars + NUL, 16 bytes
    version   LDSO_CACHE_VERSION = 1
    entries   sorted by soname, ascending strcmp order
    strings   soname and path as offsets into the table

The reader validates every offset against the file's own bounds before it
dereferences it, requires each string to be NUL-terminated wholly inside the
table, and verifies the sort rather than assuming it, since a binary search over
an unsorted table would miss. A truncated or self-inconsistent cache is refused
with a code rather than trusted. That is the same discipline the ELF parser
holds to, and for the same reason: both read a file somebody else wrote.

### Relocation

The engine implements `RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`,
`64`, `COPY`, `IRELATIVE`, `RELR`, and the static-TLS trio `TPOFF64`,
`DTPMOD64` and `DTPOFF64`, under both lazy and `BIND_NOW` binding. Each type is
certified against the best real evidence that can exist for it rather than
against a specimen that cannot. The TLS trio is held to the pinned el8
`libc.so.6`, which carries eighteen genuine `TPOFF64` relocations: the object is
mapped, its self-contained relocations applied, and every stored offset must
equal what the static-TLS layout dictates. `RELR` is factored into one decoder,
`elf_reloc_relr`, certified over a constructed stream exercising both entry
forms, because the cross toolchain emits none. The rest are certified against
dynamic specimens the cross toolchain does build.

Reaching el8's `libc.so.6` at all needs a second entry point,
`elf_reloc_apply_bootstrap`, which applies only what an object satisfies against
itself: `RELATIVE`, `RELR` and the static-TLS trio. A lone `libc.so.6` cannot
resolve what it imports from `ld.so`, and running its ifunc resolvers in a
half-built world is not something the loader will do. This is not a testing
convenience: it is the subset a real loader relocates first, the way glibc's
`ELF_DYNAMIC_RELOCATE` does relative before the rest, and it is what the loader
uses when it relocates itself before its own scope exists.

### Initialization order

A post-order depth-first walk over the dependency
edges, seeded in load order. An object is emitted after every dependency it can
reach. An edge that re-enters an object already on the walk is the edge closing
a cycle, and that single edge is dropped — not the cycle, which would discard
constraints that have nothing to do with it. Seeding in load order makes *which*
edge closes the cycle a property of the link order the program was built with,
so the order is identical on every run of the same program. Finalization is the
recorded reverse of the order that actually ran, never a second computation over
a graph a later `dlopen` may have changed underneath.

An object arriving after startup is marked late in the relocation scope and left
out of the static TLS layout entirely; it takes a dynamic module id through
`elf_tls_add_dynamic` and its block is allocated lazily per thread. The static
block's running size and next module id are scope state rather than locals, so a
later relocation pass continues the layout instead of restarting it.

### Crossing into loaded code

Every function pointer that points into a loaded
object carries `__attribute__((sysv_abi))`, through one typedef in `dl.h` rather
than at each call site, and the compiler emits the register shuffle at the
boundary. The certification's synthetic initializers carry the same attribute,
so the unit cases exercise the pointer type the loader really calls through; a
host-ABI stand-in would hide a wrong-register call until a real object was
loaded.

### Initializers before entry

After the dynamic crossing relocates a main image
and before the stub enters it at `e_entry`, the loader runs `DT_PREINIT_ARRAY`,
then `DT_INIT`, then `DT_INIT_ARRAY`, each array in forward order, skipping a
null or all-ones entry as the linker's padding. That is `dyn_init_run` in
`loader/exec/dyn_init.c`, called from the stub's dynamic branch, and it is the
same ABI order `dl_run_init` runs for the `dl` graph. An el8 program's
constructors are normally called by the C runtime it is linked with; the images
this route runs have no startup file of their own, because the crossing enters a
raw `e_entry`, so nothing would run them unless the loader does — exactly as
`ld-linux` runs the initializers of a program whose startup never gets the
chance. A program entered with its constructors unrun is a program whose globals
are unbuilt.

### The debugger rendezvous

The gdb-visible link-map node is the five-field
SVr4 prefix and only that, because that is the whole of what gdb's `solib-svr4`
reads out of target memory:

    l_addr   0    load bias: runtime address minus link-time address
    l_name   8    path string
    l_ld    16    address of the object's dynamic section
    l_next  24
    l_prev  32    NULL at the head
                  node size 40 bytes

    r_version  0    R_DEBUG_VERSION = 1
    r_map      8
    r_brk     16    address of the breakpoint function
    r_state   24    RT_CONSISTENT / RT_ADD / RT_DELETE
    r_ldbase  32
                    structure size 40 bytes

The loader's own per-object bookkeeping — search provenance, init state,
reference counts — is a separate structure that may carry a node of this exact
type as its head, so a pointer to the loader's record is a pointer to the public
node, and the public node's layout stays frozen at five fields no matter what
grows behind it.

The rendezvous is found the way SVr4 specifies rather than at a fixed address:
the loader plants the address of its one `_r_debug` into the root object's
`DT_DEBUG` entry, and a debugger reading the dynamic section follows that
pointer. `_r_debug` and the breakpoint function `_dl_debug_state` carry exactly
those names, which gdb also recognises symbolically as a fallback. The protocol
version is 1, the base SVr4 protocol; glibc's version 2 `r_debug_extended`, with
its `r_next` chain across `dlmopen` namespaces, is deliberately not claimed.
Claiming a version is a promise to lay out the field that goes with it, and that
field is meaningless until namespaces exist.

Settled by: DR-0011, DR-0016, DR-0022, DR-0025, DR-0027, DR-0059.

## The ABI seam

The boundary between System V-faced code and the Microsoft-faced runtime
beneath it. `doc/design/ABI-Boundary.md` carries the per-symbol rules and the
five kinds of divergence a shim may have to translate; this is the shape.

### Calls down

Calls into the runtime's own bodies go through generated System V faces, chosen
per export by the shape of its C prototype rather than by hand, and the
classification is itself generated and certified: 1122 integer, 307
floating-point, 10 aggregate, 222 unlisted. One `int` face shape covers every
arity, because forwarding a register the callee never reads is harmless under
both conventions. An `fp` or `aggr` face is compiled from the true prototype,
because the two conventions assign vector registers by different rules and
disagree about when an aggregate rides in registers.

Calls into a Windows import go through a signature-agnostic `ms_abi` tail jump
that repacks nothing, so one generator emits one wrapper per import and the
translation is emitted by the compiler at the call site. Callbacks come back up
through fixed per-shape compiled thunks reaching their target through a mutable
slot, so one callback of a given shape is live at a time and nothing is
generated at run time.

### Faults

Faults cross by the host's own dispatcher, and this is where the seam is
incomplete. The rule is that the convention boundary and the unwind boundary
are the same line, running through the `ms_abi` entry point, with no host
unwinder ever pointed through a System V frame. That premise does not hold for
the dispatcher's own search, which begins wherever the fault happened and has
no trampoline to route through. A System V fault on a runtime-created thread is
not delivered today: it dies on the second-chance exception.

Settled by: DR-0009, DR-0012, DR-0015, DR-0020, DR-0041, DR-0042, DR-0044, DR-0049, DR-0051, DR-0053, DR-0054, DR-0055, DR-0065.

## Veneer and classification

The glibc-ABI face itself: which symbols it exports, where their bodies come
from, and how each one is classified. Every exported name is answered from
the runtime beneath it where the runtime already has the behaviour, and the
classification tables under `veneer/libc/` record which answer each name got.
An alias is as strict as its target, which is the rule the `open64` defect
was a violation of.

### The dispositions

The veneer is the glibc-ABI face: `libc.so.6` and the eight companions, built
from the version map that `doc/design/Symbol-Resolution.md` describes. Every
name el8's glibc exports gets a disposition, and the dispositions partition the
surface with nothing left over:

    forward-alias    323   a renamed forward to another export
    forward-same    1614   a forward under its own name
    shim             222   semantics differ; a translating body
    stub            1797   nothing behind it; fails predictably
    scaffold          68   a version-node identity object
                    ----
                    4024

The first four are bodies a program can reach. The fifth is not, which is why
it is a disposition of its own rather than a stub: a version-node anchor is a
zero-content object whose name is a version string, no program calls it, and
the veneer's own versioning is what stands behind it. Filing 68 of those as
stubs would put noise into the honest inventory of what the veneer lacks, and
dropping them would leave the partition incomplete.

### The rules a table must satisfy

Three rules govern how a row gets its disposition, and they are stated with the
seam's other rules in `doc/design/ABI-Boundary.md`. An alias is as strict as
its target, to a fixed point, re-derived from the committed table so a hand
edit cannot pass. Every name on the face has a disposition, with the unlisted
set resolved in two layers — by declaration where a header declares the name,
and by hand-curated row citing a file in the pinned tree where none does. And a
stub may be filled with a synthesized body composed from the runtime, provided
the composition's delta is nil; a delta is named or the composition is refused.

### Headers are copied, not paraphrased

The header the veneer compiles against is not paraphrased. `features.h` is
el8's own arithmetic, copied, because a feature-test macro computed slightly
differently changes which declarations a package sees and therefore what it
links against.

### Two shapes worth knowing

Two families do not forward at all, and both are worth knowing as shapes rather
than as exceptions. The `stat` family crosses by its bind rather than by name,
because the `_STAT_VER` indirection is a face for something the floor does not
have. And a symbol a binary leaves undefined and marks weak is `optional`
rather than unclassified: weakness excuses an absence, so an optional symbol
does not stand between a package and `ready`. It does not conceal anything — a
weak symbol the classification map does carry still reports its own bucket,
exactly as a strong one does. Only a weak symbol the map does not carry becomes
optional.

Settled by: DR-0010, DR-0046, DR-0047, DR-0052, DR-0056, DR-0073.

## Process shape

What a process looks like from the inside, and what happens at each of its
edges. Start-up, the runtime crossing for a static image and for a dynamic
one, `main` returning, `fork` and what crosses it, signal delivery and its
frame, and a core file written from the runtime. The faced runtime hosts the
process rather than being hosted by one, and there is exactly one crossing
into it.

How a process comes to host the faced runtime at all is
`doc/design/Runtime-Crossing.md`, which is a bring-up problem rather than a
shape; this section is what the process looks like once it is up, and what
happens at each of its edges.

### Start-up and exit

`_start` zeroes `%rbp`, reads `argc` from `(%rsp)`,
`argv` from `8(%rsp)` and `envp` from `16(%rsp,%rdi,8)` — sixteen rather than
eight, because `envp` sits one past `argv`'s null terminator — parks the entry
`%rsp` in `%r12` for the `__libc_start_main` that will eventually want it,
aligns the stack, and calls `main`. The PIE variant is identical but reaches
`main` and `exit` through the GOT.

`main`'s return value leaves through `exit`, never `_exit`. That is not a
preference: glibc's startup returns from `main` into `exit`, which runs the
atexit chain and flushes stdio, and a program whose buffered output vanishes at
return from `main` is observably not this platform. A cross-built binary on the
el8 reference with stdout redirected printed nothing at all, because a full pipe
buffer died with the process. `exit` is el8's own, unchanged, so the flush and
the chain are reference behaviour by construction. A `ud2` follows the call,
because a crt that falls through is how a program ends up executing its own
`.rodata`.

### Fork

Three things do not survive Cygwin's memory copy: address space
reserved outside the host's bookkeeping, the thread pointer (which is keyed to
`NtTib.StackBase`), and the loader lock. Each gets a mechanism.

Reserved regions cross as a manifest — a little-endian byte stream with magic
`FORK`, version 1, a twelve-byte header and forty-eight bytes per region
carrying base, size, kind, protection and a twenty-four-byte name. Regions are
capped at 64, sorted by base, and the unpacker refuses a wrong magic, a wrong
version, a count over the bound, a length that does not match the count exactly,
a zero-size region, a region wrapping the address space, an unknown kind,
overlapping or unsorted regions, and an unterminated name. The count is bounded
before it is multiplied.

The thread pointer is re-established in the child from the TCB. The loader lock
is re-initialized rather than unlocked, first thing, before anything else in the
child runs.

What makes this checkable is an audit record taken in the parent and again in
the child, and compared field by field. It carries the loader's own image
address, the `r_debug` structure and its address, an FNV-1a hash of the link map
and of the object table, the search configuration, the static TLS size,
generation and module count, the DTV slot by slot, the thread pointer, and the
region table. The image address is checked first and by name, because a rebased
loader explains every other mismatch. On any disagreement the child refuses with
the field named and the two values printed, and the child's atfork handlers do
not run. This is a refusal, not a repair.

Handler ordering follows POSIX: prepare handlers in reverse registration order
before the fork, parent and child handlers in registration order after it.

### Signal delivery

The receiving thread builds its own frame. The delivering
side suspends the target, reads its context, and writes back exactly two
registers — `%rip` to the entry trampoline and `%r11` to the parameter block —
then resumes it. There is no reverse translation of a host context into a guest
one.

The frame is Linux's `rt_sigframe`: a return address, then a `ucontext`, then a
`siginfo`. Sizes and offsets are pinned by static assertion, including the one
that matters most, `oRSP` at 160 inside `uc_mcontext.gregs`, because that offset
is compiled into every handler glibc ever built.

The red zone is reserved before anything else is placed. The stack pointer is
decremented by 128 first, then the FP area is carved and aligned to 64, then the
frame, leaving `%rsp % 16 == 8` at handler entry as the ABI requires. On an
alternate stack the reservation is skipped, because the interrupted frame is not
on that stack to protect.

Return is by `iretq`. The trampoline calls into `sigreturn`, which authenticates
the frame, restores the FP state with `xrstor64` or `fxrstor64`, and hands a
register block to an assembly restorer that pushes `SS`, `RSP`, `EFLAGS`, `CS`
and `RIP` and executes `iretq`, loading `%r11` last because it is the pointer it
was reading through.

The frame carries an authenticator — a splitmix64 mix of a per-state cookie,
the frame address and the recorded top, forced odd so that a zeroed frame always
fails. Before any of it is trusted, `sigreturn` checks the frame's alignment,
canonicality, extent, authenticator, unrewritten return address, resume address
and stack pointer, selector, signal number, and the whole FP and xstate block
including that the xstate size is a multiple of eight — a condition the fuzzer
found rather than a person.

### The core file

A fatal signal is meant to leave an ELF core written by the
runtime. The writer takes a sink, a process description, and a segment list, and
emits a program-header-only image: `ET_CORE`, one `PT_NOTE` and one `PT_LOAD`
per segment, no section table at all. The notes are `NT_PRSTATUS`,
`NT_PRPSINFO`, `NT_AUXV` when an auxv is supplied, `NT_FILE` when there are
named mappings, and `NT_PRFPREG` at 512 bytes when there is FP state. Each note
is framed with namesz 5 and the eight bytes `"CORE\0\0\0\0"`. It opens no file
and walks no memory: collecting the segments is the caller's job, and under the
stub that means the link map plus `VirtualQuery`.

Settled by: DR-0029, DR-0030, DR-0033, DR-0048.

## Delivery and installation

### The manifest

The installer's only memory is a manifest under the root,
`etc/elfsysvnt/manifest`: one root-relative path per line, sorted and unique,
plain text with no header and no version. It lists every payload file, the
derived configuration it writes, the release stamp, and the loader cache when a
cache was actually rebuilt. It does not list itself.

### Idempotence and orphans

Idempotence comes from never editing in place. Every file is staged to a
`.elf-install-tmp` sibling, chmod'd, and renamed over its destination. Derived
configuration is reseeded from a pristine template each run and then has the
managed settings appended, so a hand edit lasts exactly until the next run and
no longer. A run begins by reaping stranded temporaries from a previous one.

Orphan removal is what the manifest is for. Anything in the old manifest that
is not in the new one is removed, and the directories that held it are pruned
upward until a `rmdir` fails. Ceasing to touch a file the installer no longer
manages is not enough; it has to go.

The manifest is read as hostile input, because it is a file on disk that
something else may have written. A path is refused if it is empty, absolute, or
carries any `..` or `.` component or a doubled slash, and a refused entry is
reported and left alone rather than removed. The same check runs on
destinations when writing, so a payload line cannot direct a write outside the
root.

### Atomic install

The faced DLL is installed by rename, never by copy. A copy truncates the
destination and writes 25 MB into it, and for that window a reader — a
crossing test, a parallel session — sees a partial PE and `LoadLibrary` fails
with errors 126 and 998, which are the same codes a real defect gives. Hours
have been spent mistaking that race for a bug in the DLL. The temporary is
created in the destination directory so the rename cannot cross a filesystem
and silently degrade into the copy it replaced.

One step in that sequence is not decoration: the temporary is chmod'd 755
before the rename. `mktemp` creates mode 600, and without the execute
permission the PE loader is denied the image mapping, so every process of the
faced runtime dies at load.

Settled by: DR-0034, DR-0040.

## Not verified

That the summary in The ABI seam and the detail in `doc/design/ABI-Boundary.md`
say the same thing, and likewise for the three other sub-documents. Nothing
checks a section against the document it hands off to, and the lint over the
classification tables that would check one of them is its own proposal.

Three mechanisms in this document are built, certified and unwired, and that is
the build order rather than a defect. `loader/fork/` is a closed island:
nothing outside it includes its header or calls into it. `dl_open` and
`dl_close` have no production caller anywhere. `elfcore_write` is reached only
by its own test, and the default disposition a fatal signal produces is
consumed only by one. Leaf to trunk means exactly this state — an element
certified before anything depends on it — and the packages behind all three sit
in `doc/status/delivered.txt`.

What is owed is the attachment, and each has one place it attaches. The fork
manifest is populated where the window's extent is known, which is the
placement path; the audit is what proves it, since a region table that
disagrees across the fork is what the child refuses on. The loader lock is
taken by whatever first calls `dl_open` from a running process, not by
`dl_open` itself, which is why the bracket is the driver's. The core writer is
called from the fatal-signal path, which no record has written yet.

One of the three is more than unwired. DR-0029's premise is that the low window
*is* recorded in the manifest, stated in the present tense, and no code makes
that integration. That is a destination reported as an arrival, the same
pattern as the toolchain defaults above, and it is the reason a reader should
not take the record's account of what crosses a fork as an account of what the
tree does today.

Signal delivery carries two further gaps its own record names. A signal whose
target is executing inside `cygwin1.dll` is redirected wherever it is, with no
equivalent of the vendor's deferral check, which is the largest untested case
in that package. And the `SA_RESTART` decision function exists with no caller,
so nothing consults it.

Several counts and file lists in the records have drifted from the tree, and
where this document states a number it states the tree's. The specific
divergences are recorded in the sub-document that owns each subsystem.

Nothing in the loader, the runtime or the veneer is registered in
`ci/suites.txt`. The gate runs the ELF parser suite and three documentation
checks. Every other certification named anywhere in the governed set is run by
hand, which means the non-regression guarantee several records rest on is not
enforced by anything.
