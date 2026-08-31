# DR-0042 — fault dispatch across System V frames fails on runtime-created threads

Accepted 2026-08-31. Source: WP-27 milestone 7's certification, measured
against the faced DLL. Reopens DR-0012 on its own stated terms, as a new
record pointing back rather than an edit.

## What was measured

`runtime/face/t/fault.c`, a real process of the faced runtime, takes the
same fault in three shapes. A System V fault on the process's main thread
arrives as SIGSEGV and leaves by `siglongjmp` — DR-0012's measurement,
reproduced at DLL width. A Microsoft-frame fault on a thread the runtime's
own `pthread_create` made does the same, so the delivery machinery itself
is sound on such threads. But a System V fault on that same thread does
not arrive: the process dies on the second-chance exception instead.

The difference is dispatch, not delivery. Before Cygwin's delivery can
restore a saved context, the host's dispatcher must walk from the fault
site to the frame carrying the runtime's handler. A System V frame carries
no host unwind record, deliberately (DR-0012), so the dispatcher falls
back to treating each unknown frame as a leaf — popping one slot at a time,
in effect scanning the stack for a walkable return address. On the main
thread's stack that scan happens to recover. On a pthread's stack it does
not, and the difference is stack content, not principle: the recovery was
luck both times, and DR-0012's `fault-direct-sysv` case had measured the
lucky shape.

## What this record fixes

Nothing is repaired here; the record scopes the repair. DR-0012's premise
— that no host unwinder is ever pointed through a System V frame — does
not hold for the dispatcher's own search, which has no trampoline to route
through: the search begins wherever the fault happens. The conditions
DR-0012 named for reopening are met on exactly its second branch, a path
where the host's own dispatch has to reach a handler across System V
frames.

The repair belongs to WP-43 and WP-27's milestone 8, which own the two
ends the search runs between. The candidate shapes are theirs to choose:
an anchor with host records placed immediately beneath every entry into
ELF code, a vectored handler that spares the dispatcher the walk
altogether, or delivery hooked before dispatch searches at all. Whichever
lands must make `fault.sh`'s probe — a System V fault on a
runtime-created thread, run in its own process — report `delivered`, and
that probe is this record's tripwire in both directions: fault.sh flags
loudly when its outcome changes, so a silent fix or a silent regression
both surface.

## What it does not decide

The main-thread case stays certified as it stands; nothing here withdraws
DR-0012's measurement, only its generality. The ELF-shaped signal frame
and the red-zone reservation at the delivery site stay WP-43's. Whether
`getcontext` and its family cross the face at all stays with the
context-transparent-faces record.
