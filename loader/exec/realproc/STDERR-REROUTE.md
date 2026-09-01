# Reroute of stub.c's stderr diagnostics through the write(2) crossing (WIP)

WP-56 reent-tls-bringup, item 1, implementing step. `RELINK.md` routed the
stub's stdout output (`--version`, the `--dry-run` report) through the seam and
left the stderr diagnostics deferred behind a measured stderr crossing.
`spike/reent-stub-stderr-crossing` measured that crossing: the faced
`elfsysv1.dll` exports no `stderr` `FILE*`, and a `sysv_abi write(2, s, n)`
thunk crosses it. This slice adds that thunk to the seam and reroutes the five
diagnostic paths -- `say`, `refuse`, `usage`, and the unknown-option and
no-argument messages -- through it.
