# The host-facing core (WP-22)

The runtime presents a System V face outward and speaks Microsoft x64 inward,
and the convention changes at the bottom of one DLL. This package is the set of
places Windows calls into that DLL: the DLL entry, thread starts, queued APCs,
PE TLS callbacks, vectored exception handlers, and the signal landing Cygwin's
thread-hijack delivery lands on. Each is Microsoft x64 by the host's rule and
carries the SEH unwind data the host recognizes, because Cygwin's own exception
and signal delivery is the host's SEH machinery walking MS-format records.
Below each entry the runtime is System V. Miss one and the convention leaks out
the bottom of the DLL, and the symptom is corruption rather than a link error,
which is why the milestone calls this the treacherous half.

Spike 3 measured this crossing at one function's width on 2026-08-29 and came
back yes in both directions, through a signal and through a fault, with the red
zone the one casualty and Cygwin's delivery rather than the host the thing that
takes it. What the spike did not reach was named in its README: unwind data
crossing a `sysv_abi` frame, `DllMain`, and PE TLS callbacks. This package
reaches the first of those and stands up the rest at the width a runtime that
does not yet exist allows.

## The direction this covers, and the one it does not

This is Windows calling in. Every entry point here receives a Microsoft x64
call and reaches System V code one frame down. The opposite direction -- a
System V function pointer the ELF world hands *down* to Windows, a `qsort`
comparator or a thread start routine written in the ELF world -- is WP-23's
callback trampoline, delivered in `callback.c` and documented under "The
callback trampolines" below. Where an entry point would need to forward such a
pointer rather than call a known runtime function, the attachment point is
marked `ELFSYSV_WP23_SEAM` in `core.h`, now filled. At one function's width the
down-call from an entry point to a known System V body is a direct typed call,
and the compiler emits the correct convention thunk at the call site, exactly as
spike 3 measured the callee-saved sets surviving; the trampoline is what an
entry point reaches for instead when the pointer is one a caller supplied rather
than one this unit names.

## The entry points

`entry.c` holds them, each `ms_abi` and `noinline`:

| entry point | host shape | reaches |
|---|---|---|
| `elfsysv_dllmain` | `BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)` | the core |
| `elfsysv_thread_entry` | `DWORD WINAPI (LPVOID)` | the core, returns its value |
| `elfsysv_apc` | `VOID CALLBACK (ULONG_PTR)` | the core |
| `elfsysv_tls_callback` | `VOID (PVOID, DWORD, PVOID)` | the core |
| `elfsysv_veh` | `LONG CALLBACK (EXCEPTION_POINTERS *)` | the core, then continues the search |
| `elfsysv_signal_entry` | `void (int)` | the core |

The bodies are stand-ins. A real runtime does not exist yet, so each entry point
does the least that makes the crossing observable -- reach a System V core and
come back -- rather than the runtime work it will eventually front. What is real
and certified is the shape, the convention, the unwind data, and that the
crossing holds.

## The unwind seam

The measurement that grounds this package, and the one spike 3 left open, is
recorded in DR-0012. On `x86_64-pc-cygwin` gcc 7.4.0:

  - An `ms_abi` function gets `.pdata`/`.xdata`, and the host's
    `RtlLookupFunctionEntry` -- the function the exception dispatcher itself
    calls -- returns a `RUNTIME_FUNCTION` for it. The host can walk the frame.
    A host-facing entry point is therefore `ms_abi`, never `sysv_abi`.
  - A `sysv_abi` function gets no unwind record the host recognizes, so the
    host treats a System V frame as a leaf and cannot walk it.

The second is the seam, not a defect. It is the invariant WP-23 and WP-43 rest
on: no host unwinder is ever pointed through a System V frame. Unwinding does
not cross the boundary raw -- DWARF and `.eh_frame` stay in the ELF world, SEH
stays here, and the only place they meet is a trampoline that knows to stop.
The fault path proves it holds the other way too: a store through a null
pointer taken directly in a `sysv_abi` frame, which carries no host record at
all, still reaches Cygwin as SIGSEGV and still returns, because Cygwin's
delivery restores a saved context rather than unwinding the intervening frames.
The test asserts both halves -- every entry point has a `RUNTIME_FUNCTION`, and
the System V core does not -- so a future change that started emitting host
unwind data for System V frames, silently widening what the host will try to
walk, fails here rather than in the field.

## The stand-in cores

`elfsysv_core_run` is the ELF/System V world one frame below every entry point,
a `sysv_abi` function that does a Microsoft down-call and returns a value
derived from its argument, so the crossing is exercised both ways within one
entry: Microsoft in at the top, System V here, Microsoft out through the
down-call. A stand-in core has no work whose completion a test could otherwise
see, so it records that it ran and the token it ran on in
`elfsysv_core_calls` and `elfsysv_core_last_token`. A real runtime body has its
own observable work and needs neither; the two globals are instrumentation for
the stand-in, and they are named as such rather than left to look like state
the runtime keeps.

## The fault path

The headline the milestone asks for: a fault taken inside the ELF world reaches
Cygwin's existing signal machinery and returns, with register state on both
sides matching what each convention promises. The test takes a null store
inside Microsoft code one frame beneath a `sysv_abi` frame, and separately a
null store directly in the `sysv_abi` frame, and each reaches Cygwin as SIGSEGV
and returns through `siglongjmp` past the System V frame; afterwards the
crossing is exercised again and still holds. The callee-saved sets are checked
by the hand-written probes: the full Microsoft set -- `rbx`, `rbp`, `rsi`,
`rdi`, `r12`-`r15`, `xmm6`-`xmm15` -- across an entry point, and the System V
set -- `rbx`, `rbp`, `r12`-`r15` -- across the down-call.

What is not here is WP-43's. The frame the handler is eventually given --
`siginfo_t` and `ucontext_t` in the psABI's shape, extended FPU state saved
where a consumer looks for it, `sigaltstack`, `SA_SIGINFO`, `SA_RESTART` -- and
the 128-byte reservation that repairs the red zone at the delivery site are the
signals package's work. This package proves the landing reaches System V code
and returns; it does not build the frame the ELF world reads.

## The red zone

Everything here compiles `-mno-red-zone`. The boundary between the two halves is
invisible to the compiler, so the flag is not partially applicable, and DR-0006
records it as scaffolding carried until the delivery-site repair lands rather
than the shipped answer. The test's `redzone` step confirms the flag is
actually in force -- that a `sysv_abi` leaf built with it adjusts `%rsp` where
the same leaf without it keeps its locals below `%rsp` -- so the policy cannot
go silently missing.

## The callback trampolines

`callback.c` fills the seam `core.h` marks. It is the direction this package
otherwise defers: a function pointer the ELF world writes in System V and hands
*down* to Windows, which then calls it Microsoft x64. A `qsort` comparator given
to a host `qsort`, a thread start routine given to `CreateThread`, an exception
filter given to `AddVectoredExceptionHandler` -- each is a System V body an
MS-ABI caller reaches, and the convention has to be bridged at the pointer.

The leak this prevents is the one spike 3 called the direction the design was
nervous about, and it runs opposite to the entry points'. `%rsi`, `%rdi` and
`%xmm6`-`%xmm15` are callee-saved to a Microsoft caller and volatile to a System
V callee: the caller expects them intact after the call, the callee is entitled
to have used them as scratch, and a raw pointer handed across satisfies neither
at once. The symptom is the Windows caller's own registers quietly changing, not
a crash.

Each trampoline is one `ms_abi` function whose body calls through a slot typed
`sysv_abi`. That one fact is the mechanism: from the slot's `sysv_abi` type the
compiler emits the crossing at the call -- the argument shuffle from Microsoft's
registers to System V's, and the save and restore of `%rsi`, `%rdi` and
`%xmm6`-`%xmm15` around it -- the same crossing spike 3 measured holding and the
mirror of DR-0009's down-call wrapper. The generated prologue carries `.seh_`
directives, so the trampoline frame is one the host's own unwinder can walk to
and stop at, which is the role DR-0012 reserves for it: the one place SEH and the
ELF world's DWARF meet, and it knows to stop.

The slot is a mutable pointer read at the call, because the seam forwards a
caller-supplied target rather than a symbol this unit names. There is one slot
per shape and no runtime code generation: a distinct pointer per live callback
would need a distinct compiled entry, and manufacturing entries at run time is
the self-mapped executable memory DR-0000 records the platform cannot ship. One
live callback per shape is the boundary that follows, recorded in DR-0020.
`elfsysv_cb_set_comparator`, `_set_threadproc` and `_set_exfilter` bind a target
and hand back the `ms_abi` pointer a host API is given.

## What is certified, and what is not

Certified, at the width a runtime that does not exist allows:

  - The six entry points cross to System V code and back, each `ms_abi` with
    host-recognized SEH unwind data.
  - The callee-saved set each convention promises survives the crossing in both
    directions, checked against hand-written poison rather than a compiler's
    own thunk, with leaky controls that light every bit so the check is known
    able to fail.
  - Windows entering through a thread start, a queued APC, a vectored exception
    handler and a Cygwin signal handler each reaches System V code one frame
    down and returns.
  - A fault beneath, and directly in, a System V frame reaches Cygwin and
    returns, and the crossing holds afterwards.
  - The `-mno-red-zone` policy changes code generation here, so DR-0006's
    scaffolding is in force rather than nominal.
  - The down-hand callback trampolines (WP-23): a comparator, a thread start
    routine and an exception filter each survive a round trip called Microsoft
    x64 through their trampoline with the full Microsoft callee-saved set intact
    and their arguments delivered to the System V side, the trampolines carry
    host-recognized unwind data, and the controls light so the check can fail.
    Documented under "The callback trampolines"; certified by `t/callback-run.sh`.

Not reached, and waiting on the rest of the runtime:

  - `elfsysv_dllmain` fired by the loader as a linked DLL's entry. A re-faced
    DLL is entered before this package's machinery is mapped; that is WP-41's
    neighbourhood, and only the shape, convention and unwind data are certified
    here.
  - `elfsysv_tls_callback` fired from the DLL's PE TLS directory. The directory
    exists only once the DLL does (WP-41); the callback's ABI and unwind are
    certified, its registration and firing are not.
  - A trampoline for more than one live callback of a shape at once. The fixed
    compiled trampolines hold one target per shape (DR-0020); a per-instance
    trampoline would need runtime-manufactured code, which DR-0000 forecloses.
  - The signal frame's `siginfo_t`/`ucontext_t` layout, extended state, and the
    red-zone reservation at the delivery site (WP-43).
  - `RtlUnwindEx` walking a `sysv_abi` frame and C++ exceptions crossing the
    boundary. The seam finding says the host cannot walk a System V frame and
    the fault path does not ask it to; a full cross-boundary unwind is WP-23's
    and WP-43's to build against that constraint, not to violate.

## Building and testing

    ./t/run.sh

The driver builds the core and its probe with the host gcc that targets
`x86_64-pc-cygwin` -- not the cross toolchain, because these are the functions
Windows calls into and they live in the Cygwin world -- confirms the entry
points carry `.pdata`/`.xdata`, confirms `-mno-red-zone` is in force, and runs
the crossing test. `-k` keeps the built binaries, `-q` is errors only. The test
alone, with its full table, is `core_test` built from `t/core_test.c`,
`t/probe.S` and `entry.c`; `--terse` prints the summary block a document quotes.

`t/probe.S` is hand-written for spike 3's reason: a caller the compiler wrote to
check a convention would emit the very thunk being measured, so the register
probes poison the callee-saved set themselves and read it back themselves, and
the two leaky targets destroy every register their convention must preserve so
the check proves it can fail before it is believed when it passes.

The callback trampolines have their own driver, `./t/callback-run.sh`, built the
same way and to the same discipline: it confirms `callback.o` carries
`.pdata`/`.xdata`, confirms `-mno-red-zone`, and runs `callback_test` (from
`t/callback_test.c`, `t/callback_probe.S` and `callback.c`; `--terse` prints the
summary a document quotes). `t/callback_probe.S` is hand-written for the same
reason as `t/probe.S`: its `cb_ms_probe` stands in for Windows, poisoning the
full Microsoft callee-saved set before calling the trampoline and reading it back
after; the three System V callbacks it drives destroy the scratch set on purpose
so a leak is real; and a de-bracketed trampoline and a total-leak callee are the
controls that light the check before a dark mask from the real trampoline is
believed.

## Not verified

That the transcript regenerates identically. The crossing verdict and the
register masks are deterministic and reproduced across repeated runs on
2026-08-29's machine; the callbacks case leans on the host scheduler for its
APC and signal delivery, which is timing the test waits on rather than counts.

One Windows build, one compiler: `CYGWIN_NT-10.0` under the pinned root, gcc
7.4.0 targeting `x86_64-pc-cygwin`. The unwind finding is the compiler's
behaviour on this target and should be re-measured if either moves.

That an entry point with a genuinely trivial body keeps its unwind record. The
entry points here have frames because they call; a future entry point that
optimised to a leaf could lose its `.pdata`, and the runtime check catches that
at test time rather than the shape being assumed.
