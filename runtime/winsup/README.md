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
