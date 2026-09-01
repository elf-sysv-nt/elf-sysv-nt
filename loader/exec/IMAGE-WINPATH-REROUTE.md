# Front-end image-path reroute for the real-process stub (WIP)

WP-56 reent-tls-bringup, item 1, implementing step. `spike/reent-stub-path`
found `route=parent-passes-windows-path`: the front end must hand the
real-process stub a Windows-form image path, because that stub opens with
`CreateFileA`, which cannot resolve a Cygwin mount, while the plain-PE stub's
inline `fopen` resolves the POSIX path through the host `cygwin1.dll`.

This slice wires the conversion into the front end. Work in progress.
