# The System V face at the DLL's width (WP-27)

Work in progress.

WP-26 built `elfsysv1.dll` — Cygwin 3.6.10 at the pinned ref, re-badged,
compiled `-mno-red-zone` — but its face is still Microsoft's. This package
turns the face around: the export surface re-faced per WP-20's inventory
(`runtime/exports/cygwin-exports.tsv`), the variadic entries taken from
WP-24's generated veneer, and WP-22's certified entry-point shapes now
fronting the real runtime work they were stand-ins for.

Planned order of work, one milestone per commit:

1. The face table: derive from WP-20's inventory the disposition of every
   export — data (no wrapping), function (sv2ms face), variadic (WP-24's
   generated veneer entry). A generator writes the table; a test checks it
   is total over the inventory and agrees with WP-24's list.
2. The face wiring: the generated System V export surface over the WP-26
   DLL, entry points from WP-22's `entry.c` shapes made real.
3. Rerun WP-22's and WP-23's crossing certifications against the real DLL.
4. `DllMain` and the PE TLS callback fired by the host's own loader.
5. The fault path: SIGSEGV beneath a System V frame, out by `siglongjmp`.
6. Thread creation establishing the DR-0021 carrier; per-thread blocked
   mask and alternate stack split that DR-0030 deferred.
