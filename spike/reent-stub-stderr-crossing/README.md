# reent-stub-stderr-crossing (WIP scaffold)

WP-56 reent-tls-bringup, item 1. Measures the stderr crossing the real-process
stub's diagnostics need, the one `acceptance/reent/RELINK.md` names as deferred:
`say`, `refuse`, `usage`, and the unknown-option and no-argument messages write
to `stderr`, not the `stdout` the landed `rp_puts` thunk carries, so they "want a
separate stderr crossing before they reroute."

WIP: probe and measure.sh to follow.
