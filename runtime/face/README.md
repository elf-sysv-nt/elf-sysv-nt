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
   4. The unlisted disposition — done. `derive-unlisted.sh` settles the
      222 faces the first probe could not see, from Cygwin's own
      declarations: a second `-aux-info` probe over the headers the first
      had no reason to include (the fortified ssp surface, the
      windows-typed half of `sys/cygwin.h`, the xdr headers the tree
      ships but the host does not install) resolves 144, and the 78 no
      header anywhere declares are curated in `unlisted-residue.tsv`,
      each row citing the declaring file in the pinned tree. Two classes
      join int/fp there: asis (the PE-side startup and compiler protocol
      — `_dll_crt0`, `_alloca`, the `GetCommandLine` shims — faced by
      nothing, exported unchanged) and data (`__infinity`, an object the
      `.din` fails to mark). `t/unlisted.sh` certifies reproduction,
      totality in sigclass order, the pinned counts (193 int, 16 fp,
      12 asis, 1 data), and that every citation resolves in the tree.
   5. The seam — done. The unlisted int and fp rows are folded into the
      face generators (1315 generic faces, 333 typed), each generator
      resolving sigclass's unlisted rows through `unlisted.tsv`.
      `gen-din.sh` then emits `face.din`, the export table that puts the
      faces on the DLL: one row per face-table row, data and asis rows
      passing through (with the DATA marker the vendor din omitted on
      `__infinity`), every faced row `name = __face_<name>` under the
      fence marker the face table carried out of the vendor din. The
      sigfe fence stays outside the face — export, sigfe stub, System V
      face, Microsoft body — which is sound because the stub touches
      only r10, r11, and the stack, scratch in both conventions.
      Variadic rows bind `__face_<name>` too; the build step renames
      WP-24's veneer entries onto that prefix when the objects join the
      DLL, so the seam names one convention. `t/din.sh` certifies
      reproduction, totality in face-table order, the pinned counts
      (1716 faced, 39 DATA, 12 asis), fence preservation, and that the
      vendor's own gendef consumes the file, wrapping exactly the 1000
      SIGFE-fenced faces in sigfe stubs.
   6. The context-transparent faces — done. The setjmp family cannot sit
      behind the call-style int face: its bodies capture the caller's
      `%rsp` and return address, and a longjmp restored through a dead
      face frame lands after the wrong call site once the stack is
      reused. `sv2ms-ctx.inc` is the frameless shape — two register
      moves and a jump — sound for exactly the six `ctx.tsv` rows whose
      gendef-assembly bodies never touch caller shadow space.
      `gen-ctx-faces.sh` emits them, `gen-int-faces.sh` excludes them
      (1309 generic faces remain), and `t/ctx-face.sh` certifies the
      shape and the resume-at-the-true-call-site property, with a
      call-style control showing the property can fail. The ucontext
      triple stays call-faced and unsettled; see the
      context-transparent-faces decision record.
3. The faced DLL build — done. The vendor tree takes the din and extra
   link objects from the make command line (`DIN_FILE`, `FACE_OFILES`,
   commit `5c96baaa8` there), so the face goes on without that tree
   carrying generated files. `cores.c` binds core.h inside the DLL:
   twenty-one one-to-one forwards to DLL-internal names, four written
   bodies for what Cygwin keeps static (verror, verror_at_line,
   vsetproctitle) or lacks (vsiprintf); `t/cores.sh` certifies the
   surface and that every reference resolves in the WP-26 DLL.
   `nonformat.c` now carries all fourteen prototype-driven entries and
   `nonformat-cores.c` binds their fixed-arity back ends to the DLL's
   own bodies, certified by `t/nonformat-cores.sh` to the same three
   properties. `build.sh` compiles the faces, generates the veneer
   entries onto the `__face_` prefix (dodging the host headers'
   Microsoft prototypes), renames the nonformat entries onto it with
   defines derived from the enumeration's PROTOTYPE rows, and relinks
   through the seam. Landing the link took two repairs at that seam:
   aliased data exports keep their alias in face.din (`sys_nerr =
   _sys_nerr DATA`; bare, the linker is asked to export a name the DLL
   never defines), and the vendor's gendef (commit `e506dd5f0` there)
   now publishes each sigfe stub under its aliased export name as well,
   because malloc_wrapper.cc takes the address of `_sigfe_malloc` by
   name to recognize the DLL's own export in an import table. The faced
   DLL lands at a/build/wp27-face/elfsysv1.dll.
5. Rerun WP-22's and WP-23's crossing certifications against the real
   DLL — done. `t/crossing.sh` reruns both certifications unchanged, then
   points the same instruments at the DLL itself: `crossing.c` loads the
   faced `elfsysv1.dll` (rebadged, so it coexists with the host's own
   cygwin1.dll in one process), resolves real exports, and calls them the
   way an ELF caller will — System V, straight at the export. Values
   cross the generic int face (strlen, labs, memcmp) and the typed fp
   thunks (atan2, ldexp), `sysv_cross_probe` from probe.S sees the System
   V callee-saved set survive calls into the DLL, and the leaky control
   still lights all six bits. The exports exercised are NOSIGFE leaves on
   purpose: they cross the face without the second runtime initialized,
   keeping this milestone about the face. The sigfe-fenced surface and
   the variadic veneer against the DLL need its runtime brought up
   beneath a real process, which is milestones 6-8's work.
6. `DllMain` and the PE TLS callback fired by the host's own loader — done.
   The DLL had no PE TLS directory to fire, so `tlsdir.c` gives it one:
   `_tls_used`, the symbol the linker publishes as the image's TLS data
   directory entry, naming a callback that records its firings through an
   observation seam a plain PE process can read (one environment variable,
   written with kernel32 alone, since the callback can run before
   `dll_entry` has prepared the DLL's own libc). The DLL links with
   `--gc-sections` and the loader is the unit's only referent, so the
   directory lives in named sections the vendor's linker script now keeps
   (commit `13b29ae8a` there). `t/hostload.sh` certifies it: the image
   carries a nonzero TLS data directory; LoadLibrary succeeds only after
   the DLL's own `dll_entry` ran and returned TRUE, with a leaky control —
   a DLL whose `DllMain` answers FALSE must fail to load with
   ERROR_DLL_INIT_FAILED — proving the verdict is DllMain's; and the
   observation seam counts one process attach at load, then a thread's
   attach and detach after a thread runs. Process detach stays out of
   scope: the runtime beneath the face is Cygwin, which does not support
   unload.
7. The fault path — done. `t/fault.c` is a real process of the faced
   runtime: the vendor's crt0 into the asis `_dll_crt0` protocol, linked
   -nostdlib so every libc call crosses the face System V by function
   pointer, with crt0's Microsoft-convention call to the veneer-faced
   `cygwin_internal` interposed at the link. (The cygload shape —
   LoadLibrary plus `cygwin_dll_init` — cannot carry this: the vendor
   leaves the main thread marked in-cygwin, and delivery hangs even for
   a plain raise.) `t/fault.sh` certifies the Done-when's case — a fault
   two System V frames deep on the main thread arrives as SIGSEGV and
   leaves by the faced `siglongjmp`, resuming at the true `sigsetjmp`
   site through the context-transparent face — and the same round trip
   with Microsoft frames on a thread the DLL's own `pthread_create`
   made. The third shape, a System V fault on such a thread, fails in
   dispatch and reopens DR-0012 per the fault-dispatch record: the
   repair is WP-43's and milestone 8's, and fault.sh carries the probe
   that flags the outcome changing either way.
8. Thread creation establishing the DR-0021 carrier; per-thread blocked
   mask and alternate stack split that DR-0030 deferred.
   1. The hosted carrier — done. DR-0021 reserved the backing for threads
      the runtime creates: the last data member of the forked `_cygtls`,
      one build constant below `StackBase`. The DLL hands out the calling
      thread's carrier address through `cygwin_internal
      (CW_ELFSYSV_CARRIER)` (vendor commit `a9925920a`); `carrier.c`
      latches the offset from one probe rather than a second copy of the
      constant, reads through the same %gs chain as the managed carrier,
      and wraps thread creation so the new thread writes its own pointer
      into its own carrier before the body runs — the shape the veneer's
      `pthread_create` inherits (F8). `t/carrier.sh` certifies it against
      the real DLL: one offset across threads, inside the reservation;
      a created thread finds its word zeroed and established before the
      body; the words are per thread; unaligned and disagreeing probes
      refuse, so a wrong offset cannot latch silently.
   2. The per-thread split of the blocked mask and alternate stack that
      DR-0030 deferred — remaining, with the state reached through the
      carrier's TCB rather than one process-wide record.

Landing also waits on the Done-when's last clause: a static ELF through
WP-41's branch calling a real export and returning.
