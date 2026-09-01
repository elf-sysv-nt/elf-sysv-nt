# Reroute of stub.c's image file read through the realproc seam (WIP)

WP-56 reent-tls-bringup, item 1, implementing step. `slurp` -- the stub's
`fopen`/`fread` of the ELF image and the `--elf-runtime` -- is the stub's last
direct libc use off the diagnostic paths `RELINK.md` and `STDERR-REROUTE.md`
already routed. It must go through the `realproc.h` seam too before the stub can
be linked in the real-process shape, where a plain Microsoft-into-System-V libc
call returns without crossing.

This slice is in progress.
