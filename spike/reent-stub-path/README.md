# reent-stub-path (WIP) -- the real-process stub's POSIX->Windows path question

WP-56 reent-tls-bringup, item 1. Scaffold; the measurement follows.

The image-read reroute (`loader/exec/realproc/SLURP-REROUTE.md`) closed the
stub's file I/O behind the realproc seam but left one question open: the
real-process build reads with `CreateFileA`, which resolves a Windows-form path,
while the loader is invoked with a Cygwin POSIX path (`-r /bin/echo.exe`). This
spike measures how that path is made openable host-side, without a faced-libc
call -- whether the parent that starts the stub passes a Windows path, or a
host-safe conversion inside the stub suffices.
