# DR-0046 — The unlisted faces resolve from Cygwin's own tree, and the PE protocol keeps its face

## Context

The sigclass derivation left 222 sv2ms faces unlisted: no prototype in the
probe's header set. A face cannot be generated without knowing which world
its signature lives in, so every one of them needed a disposition from
somewhere, and the only authority left is the pinned newlib-cygwin tree
itself — the same tree the DLL is built from.

## Decision

The unlisted set resolves in two layers, and the layers must partition it
exactly. A second aux-info probe covers the headers the first had no reason
to include: the fortified ssp surface, the windows-typed half of
sys/cygwin.h that only declares itself once windows.h has run, threads.h
and uchar.h, and the xdr headers the tree ships but the host never
installs. That settles 144 names by ordinary declaration. The remaining 78,
which no header anywhere declares, are curated by hand in
unlisted-residue.tsv — one row per name, each citing the file in the pinned
tree that carries the declaration or definition it claims, and the
certification greps every citation against the tree so a wrong or stale row
fails loudly.

Two classes join int and fp in the residue. asis marks the twelve entries
whose contract is with the PE side rather than with any System V caller:
the startup protocol (_dll_crt0 and its family, cygwin_dll_init,
dll_entry), the compiler helpers (__main, _alloca, __stack_chk_fail_local,
_feinitialise), and the stdcall GetCommandLine shims. Nothing faces them;
they export unchanged, because the code that calls them is Microsoft-
convention by construction and a face would only break the one caller they
have. data marks __infinity, an object the .din fails to flag as one.

## Consequences

Every export now has a settled disposition; the face generators can be
total over the surface, and the .def/.din seam can be written against a
closed table. The cost is the curated residue: 78 rows that track the
pinned tree by citation rather than by derivation. The certification keeps
them honest against that one ref, and a future re-pin surfaces every row
that no longer holds.
