# exec dispatch and the PE host stub (WP-41)

Linux decides what an executable is in the kernel and calls the table binfmt.
There is no kernel here, so the table moves into the spawn path of the library
Cygwin already funnels every host call through. This package is that move, and
the PE image that carries the ELF world's address space once the branch has
fired.

## The branch

`binfmt.c` is the whole decision, and it is pure: it opens nothing, allocates
nothing, and reads the leading bytes of a file through a callback the caller
supplies. One classifier over one read produces one of four verdicts — ELF, a
`#!` script, an image the host still owns, nothing recognized — in that order,
first match winning. DR-0027 records why the order is written down at all when
the first byte alone separates `0x7f`, `#` and `M`: the tests do not conflict
today and would begin to the first time either grew a second condition.

`binfmt_resolve` follows a `#!` chain and rebuilds the argument vector the way
the kernel rebuilds it, dropping the leading element at each hop and pushing the
interpreter, its single optional argument, and the path of the file that named
it. Four hops is the limit, and the limit is also the cycle detector: a script
whose interpreter is itself spends its hops and is refused. The `#!` line is
parsed as the kernel parses it, down to the corners — trailing blanks stripped,
the argument taken whole rather than split, an embedded NUL ending the line, and
a line that does not end within the first 256 bytes refused rather than
truncated, because a truncated interpreter path names a different program.

`dispatch.c` is the Cygwin side. It resolves, and either takes the ELF case
itself or hands the case back with the file and vector the host should use. The
handing back matters as much as the taking: a script whose interpreter turns out
to be an ordinary program is the host's to run, but only with the vector the
resolver produced, so the two paths cannot disagree about what a chain means.
`exec_main.c` is a front end that calls all of this the way the spawn path will,
which is what the certification runs against while winsup is built elsewhere.

## The window

`reserve.c` holds the address range the ELF world lives in, and it is not held
by the process that uses it. The measurement in `t/when-2026-08-30.txt` asked
where a reservation at `0x400000` can be made from and found that nothing inside
the image is early enough: the kernel places the initial thread's stack there
while the process is being created, and `cygwin1.dll` chews the region below it
into small mappings, both before the image's first instruction. A TLS callback
cannot even be written on this toolchain — Cygwin supplies no `_tls_used`, so
there is no TLS directory — and a replacement entry point is refused with the
stack sitting in the window.

So the parent reserves it, through `VirtualAllocEx`, into a child created
suspended, and the stub adopts what it finds. The stub is linked with a
`0x100000` stack reserve so the kernel does not put the child's stack in the
window; with the default two megabytes the reservation is refused every time,
which the measurement's fifth case shows. DR-0028.

Handing the window over is the awkward part. A Windows reservation cannot be
partially released, so `elf_window_yield` releases it, places the image while
the address space is bare, and re-reserves the remainders. Nothing may allocate
in between, which is a contract the caller keeps.

## The stub

`stub.c` is the image Windows thinks it is running. It adopts the window,
confirms the process is not running under Control Flow Guard or user-mode shadow
stacks — neither of which this toolchain opts into, so the check is that the
absence held rather than a switch being thrown — parses through WP-31, places
through WP-32, builds the initial stack through WP-40, and enters. `enter.S` is
the crossing, and it is one-way: a real exec does not come back, so nothing is
saved and no resume address is parked.

Two things there were measured rather than assumed, both by the specimen dying
first. The host's C library cannot be called while the ELF stack is in force,
because Cygwin finds its per-thread state from the stack pointer, so `elf_enter`
parks the host stack and `elf_terminate` restores it before crossing back. And
the shadow space a hand-written thunk reserves has to be counted for the stack
it actually has rather than copied from a prologue that pushed an odd number of
words; the forty bytes that are right after eight pushes leave `%rsp` eight past
the boundary here, and the first aligned move in the callee faults.

`elf_terminate` is the value the stub puts in `%rdx`, which the psABI reserves
for the termination handler a program registers. Here it ends the process, and a
program with no startup file — which is what the certification specimen is —
jumps to it with a status in `%rdi`. It is the one way out of the ELF world that
does not need the syscall surface a later package delivers.

Between the parse and the entry the stub asks one more question: which crossing
the image is owed. `exec_kind_of` (WP-56) reads the parsed image — its `e_type`
and whether it names a `PT_INTERP` — and the stub branches on the verdict, the
single decision DR-0058 places here. A static executable keeps the direct entry
above, the path this package certifies. A dynamic image, one that names an
interpreter (bzip2's shape), may not be entered at `e_entry`: its `_start` runs
before the GOT is relocated against the runtime, so entered that way it faults
on its first library call. So the stub runs the crossing first — DR-0058's
`dyn_exec_link`, over the loader packages already delivered — which maps the ELF
runtime named by `--elf-runtime`, links the image against it so its GOT and PLT
resolve into the runtime's exports, runs its initializers, and only then enters.
With no runtime named the image is refused rather than entered into the fault.
`dyn_init` runs the image's `DT_INIT` chain — DT_PREINIT_ARRAY, DT_INIT,
DT_INIT_ARRAY, the ABI's order — between the link and the entry, so a program
reaches `e_entry` with its constructors run, the way `ld-linux` would have run
them (a DR settles the step, and the Microsoft-to-System V ABI bridge the call
crosses). A specimen with no initializers reaches the entry having run none. An
image this route does not run at all — a relocatable object, a core, a bare shared object — is
refused before it is even mapped.

## What is certified

`t/run.sh` builds all of it and holds it to the done-when. The classifier, the
`#!` line, the kernel's vector rebuild, the depth limit and the host's
command-line quoting are checked as pure decisions over fixtures; the classifier
is then fuzzed against malformed and truncated heads placed against a guard
page, under the undefined-behaviour sanitizer; the window measurement is rerun
and each route has to give the answer the transcript records.

Then the end to end. A static ELF linked at `0x400000` is run from a Cygwin
program through the branch, and the specimen reports by leaving: seven bits, one
per check — argc, `argv[0]`, the envp terminator, `AT_PAGESZ`, the psABI's
alignment at `_start`, its own read-only sentinel, and a zero past `p_filesz` —
so a status of 127 is the only pass. A `#!` script still works, a two-hop chain
comes out in the kernel's order, a cycle is refused by name, and a host image
comes back as the host's rather than as an error.

The stub's own branch is then held to each shape it must tell apart. The static
specimen still leaves 127 — the classification did not disturb the path it
guards. A bare shared object — a dynamic section, no interpreter — is refused as
no program on this route, before it is mapped. And the crossing itself is run
end to end: an interp-bearing `ET_EXEC` whose entry calls one function imported
from an ELF runtime is linked against that runtime through the stub and entered,
and it leaves with what the call returned — a status reached only when its PLT
was relocated into the runtime. The same image with no runtime supplied is
refused rather than entered into the fault. The runtime here is a bare specimen,
not the WP-53 `libc.so.6` veneer; standing the branch up against a specimen is
this step, and the veneer and bzip2 are the ones it carries.

The initializers are certified the same way, by a specimen that can only reach
its status through the right order. Its DT_INIT sets a global to 2 from 0, and
its DT_INIT_ARRAY entry, seeing that 2 and `greet()` returning across the
crossing, sets it to 42, which the entry carries out; any other order, a skipped
stage, or an unresolved call poisons it off 42. The global is 0 in the image, so
42 is reached only when the loader ran DT_INIT first and DT_INIT_ARRAY second,
both before the entry — and, because the initializers are System V code called
from the Microsoft-ABI stub, only when that call crossed the ABI boundary
without losing its arguments or corrupting its caller, which an ordinary call
does and the measured first cut did.

The quoting has its own tests for a reason worth repeating. It is sized with no
buffer and then written into one, and the first version did that with a macro
whose argument carried the increment that advanced through the string. On the
sizing pass the store the macro guarded never ran, so neither did the increment,
and the function did not return. The test now requires the two passes to agree.
