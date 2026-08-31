# DR-0045 — The runtime crossing for a static ELF

WP-27's done-when closes on a static ELF that goes through WP-41's branch,
calls a real export of the faced DLL, and returns. That takes three things
none of the delivered packages had settled: who loads the runtime into the
ELF process, how the image finds it, and what keeps the load from dying in
the runtime's own fault handlers. This record settles all three.

## Who loads it

The stub. The plan already gives WP-41's stub the job — "loads the runtime"
is in its Delivers — and the stub is the only party positioned to do it
after the low window is held, so nothing the host's loader allocates can
land where the image must go. The stub takes the runtime's host path from a
new `--runtime` option; the front end carries the option through its
existing stub-options seam (`ELFSYSV_STUB_OPTIONS`), which is how the spawn
path will carry it too. With no `--runtime` nothing is loaded and nothing
changes, which is the shape the WP-41 certification runs unchanged.

## How the image finds it

`AT_BASE`. The auxiliary vector already carries the entry, the builder
already plumbs it (`proc_image_params.base`), and its defined meaning — the
base address the interpreter was loaded at — is the nearest thing this
process has to the truth: the runtime is the interpreter-shaped party here,
the thing loaded beside a static image that the image calls into. A static
Linux binary reads `AT_BASE` as 0 and ours still does when no runtime is
loaded, so no existing meaning is displaced. The elfcall certification
walks from `AT_BASE` through the PE export directory to real exports and
calls them System V, which is exactly the walk a future libc startup can
make.

## Why the load rides its own thread

Loading the faced DLL on the stub's main thread died or survived with the
layout of `main`'s stack frame, measured to the frame: the same load, same
arguments, same parent, crashed with one `main` and completed with a
smaller one. The mechanism is DR-0003's carrier read from the wrong side:
Cygwin locates a thread's control block a fixed distance below the stack
base, and its vectored fault handler reads the resume chain there on every
exception — including the intentional probe its own initialization raises —
before anything has initialized that region. On the main thread of a native
image that region holds whatever the C runtime's and `main`'s frames put
there; a nonzero word where the handler expects its chain sends it through
a wild pointer, the handler's own fault re-enters it, and the recursion
ends in a stack overflow.

Every process that loads this runtime bare and survives — cygload, the
hostload certification — gives it a main thread whose stack top is nearly
fresh. The stub reproduces that shape deliberately: it loads the runtime on
a thread of its own with a small reserve, whose stack top carries only the
thread-start frames with zeroed stack beneath them, and joins it before
going on. The load is not concurrent; the thread is the shape of the stack,
not a scheduling choice.

## The certification's own shape

The sole-runtime-crossing proposal stands: the faced DLL is exercised as
its process's only Cygwin runtime, so the certification's stub is stub.c
built native (mingw), with the POSIX memory calls beneath the certified
mapper supplied by a Win32 shim (`runtime/face/t/shim`). The shim is test
scaffolding, not product: in the product the stub is a program of the faced
runtime and those calls are the runtime's own, which is what keeps the
mapper's mmap visible to the fork bookkeeping WP-42 needs. The certified
mapper, parser, stack builder, branch, and front end compile for the
certification unchanged.
