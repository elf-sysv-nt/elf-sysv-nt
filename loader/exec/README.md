# exec dispatch and the PE host stub (WP-41) — work in progress

The magic-byte branch in Cygwin's spawn path and the PE image that carries the
ELF world's address space. Nothing here is finished yet; this file marks the
package as started so the work is visible before it is done.

Planned contents:

  - `binfmt.c` / `binfmt.h` — the dispatch: read the leading bytes of an
    executable, classify it as ELF, `#!`, or leave it to the host, resolve an
    interpreter chain under a written recursion limit, and build the argument
    vector each case is entered with.
  - `reserve.c` / `reserve.h` — the address-space reservation the stub makes
    before anything else in the process allocates, and the PE TLS callback that
    runs it early enough to matter.
  - `stub.c` — the host image: reserve, opt out of CET shadow stacks and
    Control Flow Guard, load the runtime, hand the loader the file and the
    argument vector.
  - `t/` — the certification, including the measurement WP-41's plan section
    asks for first: whether a TLS callback runs ahead of the allocations that
    would otherwise take `0x400000`.
