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
