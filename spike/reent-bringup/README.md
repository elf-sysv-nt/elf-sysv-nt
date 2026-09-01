# reent bring-up: which host shape makes a reent-consuming body work

WP-56's road-to-green row `reent-tls-bringup` asks whether the runtime's
reent/TLS structure is set up so libc calls that consult it (errno, and the
string/locale bodies behind it) work at runtime rather than merely link. The
live crossings so far are freestanding specimens that deliberately never bring
the runtime up (`veneer/wiring/t/live-string.sh`), so they leave the question
open. This spike answers it by measuring the machine.

It contrasts the two host shapes a faced-runtime call can be made from:

  - the *cygload* shape -- a foreign PE that `LoadLibrary`s `elfsysv1.dll` and
    reaches its exports, which is what WP-41's stub is today; and
  - a *real process of the faced runtime* -- an exe linked `-nostdlib` against
    the WP-26 `crt0.o` and `-lcygwin`, so startup runs the `_dll_crt0`
    protocol, the shape `runtime/face/t/fault.c` already uses.

## The question

Does a reent-consuming libc body, called across the System V face, read and
write the same per-thread reent the caller sees -- and which host shape does it
take to make that hold?

## Running it

    ./measure.sh                 # build the probes, run all three, print findings
    ./measure.sh -o results-$(date +%F).txt

Both the faced DLL (`a/build/wp27-face/elfsysv1.dll`) and the WP-26 build tree
(`a/build/wp26/.../crt0.o`, `libcygwin.a`) are build products and are not
committed, so `measure.sh` reports SKIP when either is missing.
