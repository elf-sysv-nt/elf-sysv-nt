# reent-stub-libc-crossing -- does a host->faced libc call cross a System V thunk (WIP)

WP-56 `reent-tls-bringup` road, item 1 of `acceptance/reent/README.md`.
`spike/reent-stub-realproc-window` measured that an ordinary Microsoft-ABI host
call into the faced System V libc does not cross (`ms_abi_libc_call_crosses=no`),
while a `cygwin_internal` reached through an explicit System V bridge does. That
leaves `acceptance/reent/stub-realproc.md`'s open "bounded choice": whether the
stub can reach the faced libc through a per-call System V thunk at all, and if
so, for which calls.

This spike measures that, split so the answer is informative either way:

  - a reent-free call (`strlen`) reached through a `sysv_abi` thunk, and
  - a stdio call (`printf`) reached the same way,

so a `strlen` that crosses while `printf` does not isolates the remaining
obstacle to reent/stdio bring-up rather than the ABI thunk itself.

WIP -- measure.sh and the probe follow.
