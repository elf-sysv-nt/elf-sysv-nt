# reent-stub-realproc-window -- WIP

Advancing WP-56 reent-tls-bringup item 1 (acceptance/reent/README.md): the
loader's PE host stub, relinked in the real-process shape (-nostdlib + WP-26
crt0.o + -lcygwin), faults before entry when run standalone -- _dll_crt0 lays
out its low mappings over the non-PIE image's 0x400000 window (reserve.h
ELF_WINDOW_BASE). spike/reent-stub-link measured the fault; this spike locates
it, so item 1's window/image-base reconciliation rests on where the collision
actually is rather than on the account of it.

Status: scaffold. measure.sh and results to follow.
