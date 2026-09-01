# Real-process stub link — reent bring-up rung, item 1 (WIP)

`acceptance/reent/README.md` item 1 asks that the loader's PE host stub be
relinked in the real-process shape — `-nostdlib` against the WP-26 `crt0.o`
and `-lcygwin`, so `_dll_crt0` brings the reent up the sanctioned way — without
regressing the WP-41 exec-* certifications the plain-PE stub passes today.

This session builds that link as a *separate* target rather than replacing the
certified plain-PE stub, and certifies that the real-process-shaped stub links
and starts (reaches `--version` through `_dll_crt0`, which needs the reent).

Certification: `loader/exec/t/reent-stub.sh` (SKIPs when the WP-26 build tree or
the faced DLL are absent, both uncommitted build products).
