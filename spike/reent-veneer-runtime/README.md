# reent-veneer-runtime -- can the WP-53 libc.so.6 veneer carry a reent?

The `reent-tls-bringup` rung of `acceptance/to-green.tsv` (WP-56's road to
green) needs a reent-bearing ELF runtime the loader crossing resolves
`libc.so.6` against. `acceptance/reent/README.md` names WP-53's veneer as that
runtime, item 2 of the rung. This spike measures what the veneer, built today,
provides toward it, so item 2 rests on a reproduced fact rather than a plan --
the companion to `spike/reent-stub-link/`, which measured item 1.

## The finding, reproduced

`measure.sh` (run 2026-09-01) builds the veneer with `veneer/libc/build-libc`
into a scratch tree and reads the result:

  - `veneer_libc_builds=yes`. The library builds unchanged.

  - `reent_surface_present=yes`. It carries the whole reent *surface*: the
    `errno@@GLIBC_PRIVATE` TLS carrier the reent hands back, and reent-consuming
    bodies like `strtol` at the `GLIBC_2.2.5` node el8 assigns them.

  - `reent_body_is_stub=yes`. But every FUNC/IFUNC body is a single-byte `ret`:
    no non-stub body exists, and the byte at `strtol`'s entry is `0xc3`. The
    `elfsysv1.dll` export each entry is to reach is recorded in
    `libc-forward.tsv` as data, not emitted as forwarding code.

So the veneer resolves the crossing's `libc.so.6` imports at *link* time and
consults no reent at *run* time. Item 2 is therefore generating the forwarding
bodies that reach `elfsysv1.dll` -- where the WP-27 face brings the reent up --
not merely building the veneer. That, item 1's real-process stub startup, and a
reent-consuming ELF specimen across the crossing (item 3) are what stand between
this surface and the `reent-tls-bringup` signal.

## Running it

    bash measure.sh                 # writes the transcript to stdout
    bash measure.sh -o results.txt  # or to a file

It SKIPs (verdict yes) when the cross toolchain is absent, that being an
uncommitted build product. Registered in `test/spike-regen.tsv`, so a rerun that
no longer reproduces fails the same way a broken unit test does.
