# WIP — runtime-resolving thunk codegen (WP-56, reent rung item 2)

`spike/reent-veneer-thunk` pinned the link-time contract a real veneer body must
meet: a versioned definition whose target is a `.rodata` name resolved at run
time through one hidden per-veneer resolver, with no ELF dependency on the faced
name. This branch makes `veneer/libc/generate.py` emit that shape in place of the
`ret` stub, adds the committed hidden resolver the bodies call, and certifies the
four spike facts on the *built* `libc.so.6` rather than on a hand-built probe.

Scaffold commit; implementation follows.
