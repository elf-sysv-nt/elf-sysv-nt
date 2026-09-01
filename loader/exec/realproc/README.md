# Real-process stub compatibility layer (WP-56 reent-tls-bringup, item 1)

WIP. `acceptance/reent/README.md` item 1's empirical phase is closed
(DR-0066): a real-process host stub -- linked `-nostdlib` against the WP-26
`crt0.o` and `-lcygwin`, so `_dll_crt0` brings the reent up -- faults only at
the crt0 startup `cygwin_internal` crossing, and its own libc use is a
Microsoft-into-System-V call that does not cross. The demonstrated fixes are a
`sysv_abi` startup bridge and routing the stub's own work off the faced libc.

This directory turns those measured fixes into a reusable, certified layer the
implementing stub relink links, rather than re-linking the plain-PE stub the
WP-41 exec-* certifications drive (which stays untouched).
