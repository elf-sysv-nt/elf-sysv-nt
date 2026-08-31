# The System V face at the DLL's width (WP-27)

Work in progress.

WP-26 built `elfsysv1.dll` — Cygwin 3.6.10 at the pinned ref, re-badged,
compiled `-mno-red-zone` — but its face is still Microsoft's. This package
turns the face around: the export surface re-faced per WP-20's inventory
(`runtime/exports/cygwin-exports.tsv`), the variadic entries taken from
WP-24's generated veneer, and WP-22's certified entry-point shapes now
fronting the real runtime work they were stand-ins for.

Planned order of work, one milestone per commit:

1. The face table — done. `gen-face.sh` derives `face.tsv` from WP-20's
   inventory and WP-24's variadic list: one row per export, disposed
   data (exported unchanged), variadic (WP-24's generated veneer entry),
   or sv2ms (a System V face over the Microsoft body), each bound to its
   .din target — an alias like `accept = cygwin_accept` binds the face to
   the DLL-internal symbol. `t/face-table.sh` certifies it reproduces, is
   total over the inventory (1767 rows: 38 data, 68 variadic, 1661 sv2ms,
   101 aliased), and that no variadic export is an alias.
2. The face wiring: the generated System V export surface over the WP-26
   DLL, entry points from WP-22's `entry.c` shapes made real.
   1. The signature classes — done. A face over an all-integer body needs
      no arity; any float or by-value aggregate in the signature changes
      the registers and needs the true prototype. `derive-sigclass.sh`
      reads the prototypes out of the host headers with `gcc -aux-info`
      and writes `sigclass.tsv`: one row per sv2ms face, classed int
      (1122), fp (307), aggr (10), or unlisted (222, the Cygwin-internal
      and underscore names no public header declares). `t/sigclass.sh`
      certifies reproduction, totality over the face table, and the
      pinned counts.
   2. The generic int face — done. `sv2ms-int.inc` is the one shape the
      whole int class shares: four register moves, two register-to-slot
      moves, an unconditional eight-slot stack copy (headroom over the
      widest int signature, arity ten), one sub for shadow space and
      Microsoft alignment. `gen-int-faces.sh` instantiates it once per
      int-class row as `__face_<name>` bound to the row's target; the
      export table renames the face back at the `.def`/`.din` seam.
      `t/int-face.sh` certifies reproduction, exact coverage of the int
      class in order, that the unit assembles, and — through Microsoft
      bodies of arity 0, 4, 6, and 10 called from a System V caller —
      that every argument and return crosses exactly, at Microsoft stack
      alignment.
   3. The typed faces — done. A float, double, _Complex, or by-value
      aggregate anywhere in the signature changes which registers the two
      conventions use, so each fp (307) and aggr (10) face is a C thunk
      compiled from its recorded prototype: a `sysv_abi` function over a
      Microsoft body reached through an `__asm__` label bound to the
      face-table target. `gen-typed-faces.sh` emits `typed-faces.gen.c`;
      `t/typed-face.sh` certifies reproduction, exact coverage of both
      classes in order, that the unit compiles clean against the host
      headers, and — driving the same emission over fabricated tables —
      that doubles past both register files, mixed signatures, floats,
      long doubles, complex values, and structs and unions in every
      passing class cross exactly.
   4. The unlisted disposition read from Cygwin's own declarations, and
      the `.def`/`.din` seam that puts all the faces on the DLL.
3. Rerun WP-22's and WP-23's crossing certifications against the real DLL.
4. `DllMain` and the PE TLS callback fired by the host's own loader.
5. The fault path: SIGSEGV beneath a System V frame, out by `siglongjmp`.
6. Thread creation establishing the DR-0021 carrier; per-thread blocked
   mask and alternate stack split that DR-0030 deferred.
