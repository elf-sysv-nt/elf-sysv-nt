# reent bring-up across the loader crossing (WP-56 road-to-green rung)

WORK IN PROGRESS. This directory certifies the `reent-tls-bringup` rung of
`acceptance/to-green.tsv`: the runtime's reent/TLS structure is set up so libc
bodies that consult it (errno, and the string/locale bodies behind it) work at
runtime rather than merely link.

DR-0060 settled the shape. A freestanding crossing specimen (the live-* wiring
harnesses) reaches the faced runtime's exports but never brings the reent up, so
a reent-consuming body returns garbage there. The bring-up is the
real-process-of-the-faced-runtime shape: the host that carries the ELF world is
linked `-nostdlib` against the WP-26 `crt0.o` and `-lcygwin`, so startup runs
the `_dll_crt0` protocol and the reent is brought up the sanctioned way.
`spike/reent-bringup/` measured it and its `realproc_body_sets_errno_erange`
reproduces.

What lands here: a live test that runs a NOSIGFE reent-consuming body
(`strtol` on an overflow) through the real-process crossing shape and asserts it
returns `LONG_MAX` and sets `errno` to `ERANGE` in the very reent the faced
`__errno` hands back, and the `to-green.tsv` signal wired to it.
