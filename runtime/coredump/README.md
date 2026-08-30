# ELF core dumps

WP-61, under DR-0033. A fatal signal leaves an ELF core, and the runtime is
what writes it: an ET_CORE image in the layout the Linux kernel writes,
which the WP-60 gdb reads through the same code that reads a core from a
real Linux machine.

    t/run.sh

`elfcore_write` is the whole export. It takes the register file the
delivery path captured (DR-0030's `elfsysv_sigctx_t`), a description of
the process, and a list of memory segments, and emits the image through a
caller-supplied sink -- NT_PRSTATUS, NT_PRPSINFO, NT_AUXV, NT_FILE and an
NT_PRFPREG when the capture carried an fxsave image, then a PT_LOAD per
segment. It opens no file and walks no memory; collection is the caller's
side of the seam, which is what lets the certification drive it without a
crashing process.

## The certification, and what it must defer

`t/run.sh` fabricates a process image with planted values, writes the core,
and reads every planted value back through the consumers rather than
through a parser of our own: the cross readelf for the container, the
WP-60 gdb for the registers, the word at the interrupted stack pointer,
the argument string and the NT_FILE mapping list. The cross readelf is a
32-bit build and cannot decode the 64-bit NT_FILE desc; gdb decodes it in
full, so the mapping check lives on the gdb side.

What a live fault adds is collection: the fatal path takes the
`ELF_SIG_DEFAULT` disposition from `sigdisp` with the context already in
hand, gathers the segments from the link map and `VirtualQuery`, and calls
the writer before it exits. That path is unwritten because the loader
cannot yet run a process to crash -- the same boundary WP-15's and WP-60's
acceptances recorded. When a stub process runs, wiring the call is the
work, and this file's planted-value bar becomes a crash-and-debug bar.

## When the crash leaves a minidump instead

A crash the runtime never sees -- in the stub before the runtime is up, or
host-side machinery faulting outside the delivery path -- leaves whatever
the host's crash reporting is configured to leave, normally a Windows
minidump of the stub. That file holds the Windows view: a PE module list
naming the stub and `elfsysv1.dll`, anonymous executable regions where the
ELF objects live, and raw thread contexts. No ELF-aware tool reads it, and
gdb will not. Working from one means WinDbg or cdb: the thread contexts
give rip and rsp, and an address inside an anonymous region is mapped to
an ELF object by hand, by matching it against the load addresses in the
runtime's link map (WP-39's `r_debug` chain, reachable in the dump's
memory if it was captured with full memory). It is archaeology, not
debugging, which is why DR-0033 keeps it the fallback and puts the core
writer in the runtime.
