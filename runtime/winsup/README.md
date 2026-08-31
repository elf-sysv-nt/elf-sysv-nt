# winsup build — elfsysv1.dll (WP-26)

Work in progress.

Builds the `newlib-cygwin` tree at the pinned ref `b11613e47` (Cygwin
3.6.10). Per DR-0002's pattern the source lives outside the repository, at
`/c/-/repo/newlib-cygwin`, with this project's re-face commits on top.
Everything compiles `-mno-red-zone`; the output DLL is `elfsysv1.dll`, a
re-badged Cygwin whose face is still Microsoft's.

`build.sh` configures and builds out of tree under `a/build/wp26` and logs
to `a/build-logs/wp26-winsup-dll.log`. Neither the build tree nor the log
is committed.

## Build residue (WP-16-style ledger)

- The toplevel make links `new-cygwin1.dll` (24 MB) cleanly under
  `-mno-red-zone`; the DLL itself compiles without incident.
- `winsup/utils/dumper.cc` still used `bfd_boolean`, which binutils
  removed from bfd.h. Fixed on the vendor tree as commit `6fd9f5718`
  ("Cygwin: dumper: use bool for the removed bfd_boolean") on top of
  the re-badge commit `77121154e` over the pin `b11613e47`.
- The toplevel build now completes; `elfsysv1.dll` is the export name
  and `runtime/winsup/t/smoke-hello.sh` proves a hello runs against it
  from a native (cmd.exe) parent. Spawned from a Cygwin parent the
  child-info handshake collides: bash passes its cygheap and the badge
  reads it and dies. Handshake separation is re-face work, deferred.
- `_cygtls` now carries DR-0021's reserved field: `elfsysv_carrier`, the
  last data member, added on the vendor tree as `c86f4eeec` ("Cygwin: add
  the DR-0021 reserved carrier field to _cygtls"). Two `static_assert`s
  after the class guard that it stays last and that the struct still fits
  `__CYGTLS_PADSIZE__`, so its distance below `StackBase` is the build
  constant `__CYGTLS_PADSIZE__ - sizeof (_cygtls) + 8`. From the DLL's
  debug info that is `12800 - 5344 + 8 = 7464`, and `runtime/tls/measure`'s
  3.6.10 rerun confirms the slot from the other side (see its README).
- `repro.sh` answers the reproducibility clause: a second clean build under
  `a/build/wp26-repro`, then a section compare of the two DLLs — `.text`
  and `.data` must match; `.rdata` may embed paths and is reported only.
  It logs to `a/build-logs/wp26-repro.log`.
- The first repro run failed at `dumper.exe`: the first build had gained
  its `-lzstd` from a hand edit to the generated utils Makefile, which no
  fresh build repeats. Recorded fix: pass `LDFLAGS_FOR_TARGET=-L$deps` at
  configure so winsup's own `AC_CHECK_LIB([zstd])` finds the stub and puts
  `-lzstd` into `BFD_LIBS` itself. Both `build.sh` and `repro.sh` now pass
  it, and `repro.sh` creates the stub for itself.
