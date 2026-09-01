# Relink of loader/exec/stub.c against the real-process layer (WIP)

WP-56 reent-tls-bringup, item 1, implementing step. DR-0066 closed the
empirical phase; `loader/exec/realproc/` is the certified foundation. This
branch wires `stub.c` to that foundation through the `realproc.h` seam:

  - `stub.c` includes `realproc.h` and routes the libc operations the layer
    covers -- its option parsing (`strcmp`/`strncmp`/`strtoull`) and its
    `--version` output -- through the `RP_*` macros.
  - Without `ELFSYSV_REALPROC` the seam is the identity, so the plain-PE build
    the WP-41 exec-* certifications drive is unchanged. The proof is object-code
    equality of `stub.o` before and after, not an assertion.
  - Under `ELFSYSV_REALPROC` the same source links against the layer's
    freestanding primitives and its `sysv_abi` output thunk.

Scope: the covered operations only. The stub's other libc use (the Windows
placement path, `fprintf` diagnostics, `slurp`) is out of item 1's version-path
scope and is not rerouted here; item 3's live crossing stays deferred.
