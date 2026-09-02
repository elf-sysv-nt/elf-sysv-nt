# The faced-runtime crossing host (WP-56 reent-tls-bringup, DR-0071 finishing step)

DR-0071 decided the acceptance crossing hosts the faced runtime as its own
process: a real process of `elfsysv1.dll`, brought up through the WP-26 `crt0`
`_dll_crt0` protocol so the faced runtime is the process's sole Cygwin runtime.
The image (bzip2) is mapped through that faced runtime's own `mmap` (the DR-0008
mapping) and entered inside the process, and `AT_BASE` carries the faced
runtime's own base -- laid down by its own startup -- so the veneer's forwarding
thunks resolve against a live face.

This directory holds that crossing host: the build recipe that links it in the
real-process shape, and the entry path that maps and enters the ELF image inside
it. It reuses the loader's certified map/enter units (`loader/map`, `enter.S`,
`dyn_exec.c`) and the `loader/exec/realproc/` host-safe seam; what is new here is
the link shape (crt0 + faced runtime, not plain PE + host cygwin) and the
`AT_BASE = own faced base` publication.

Built and certified as its own step, per DR-0071: the plain-PE stub the WP-41
exec-* certifications drive is untouched -- this is a separate host, not a
replacement -- until the cutover of `accept.sh`'s `build_loader` is certified
against that bar.
