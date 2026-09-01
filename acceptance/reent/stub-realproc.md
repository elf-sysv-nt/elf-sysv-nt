# Real-process stub link -- reent bring-up rung, item 1 (measured)

`acceptance/reent/README.md` item 1 asks that the loader's PE host stub be
relinked in the real-process shape -- `-nostdlib` against the WP-26 `crt0.o`
and `-lcygwin`, so `_dll_crt0` brings the reent up the sanctioned way -- without
regressing the WP-41 exec-* certifications the plain-PE stub passes today.

`spike/reent-stub-link/` measures how far a link change alone carries that, and
the answer reproduces on this tree (measure.sh, 2026-09-01):

  - `realproc_stub_links=yes`. The stub's whole translation-unit set links in
    the real-process shape, once `-lgcc` supplies the compiler builtins
    (`__chkstk_ms`) that `-nostdlib` drops. So the *link* half of item 1 holds.

  - `realproc_stub_reaches_version=no`. The linked stub, run standalone, faults
    during startup before it reaches even its `--version` path. The stub is a
    minimal non-PIE PE that adopts a parent-reserved low window (DR-0028); run
    as a real process of the faced runtime, `_dll_crt0` lays out its own low
    mappings and the two collide before `main`.

So item 1 is not a link-flag change: the stub's window/image-base contract has
to be reconciled with real-process startup, which is the WP-41/WP-43-shaped work
DR-0060 and the README already name. The plain-PE stub the exec-* certifications
drive is untouched -- this rung builds the real-process link as a separate
measurement, not a replacement. Item 3 (a reent-consuming ELF body across the
crossing) stays deferred behind this and the WP-53 `libc.so.6` veneer.
