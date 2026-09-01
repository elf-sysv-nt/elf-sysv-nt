# reent-veneer-body -- what a real forwarding body must reach, and how

WIP scaffold. This spike measures item 2 of the `reent-tls-bringup` rung
(`acceptance/reent/README.md`): the WP-53 `libc.so.6` veneer carries the reent
surface but every FUNC/IFUNC body is a single-byte `ret`
(`spike/reent-veneer-runtime`). Item 2 is generating the bodies that reach
`elfsysv1.dll`. Before that codegen is written, two facts about its target
surface need measuring rather than assuming.
