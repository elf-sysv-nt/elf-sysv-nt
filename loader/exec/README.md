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

The quoting has its own tests for a reason worth repeating. It is sized with no
buffer and then written into one, and the first version did that with a macro
whose argument carried the increment that advanced through the string. On the
sizing pass the store the macro guarded never ran, so neither did the increment,
and the function did not return. The test now requires the two passes to agree.
