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

## Where the crossing host halts today (measured)

`build-host.sh` links the host in the real-process shape (the exact recipe
`spike/reent-stub-realproc-run` proved) and `drive.sh` runs an ELF image through
it, detached and beside `elfsysv1.dll`. The low-window step is now cleared:
`elf_window_reserve` gained an `ELFSYSV_REALPROC` branch (`loader/exec/reserve.c`)
that claims the window as bookkeeping without a host reservation, and
`elf_window_yield`'s `ELFSYSV_REALPROC` branch hands the bare, already-free low
window straight to the placer with no MEM_RELEASE dance, since a real process of
the faced runtime laid its own space and the window reads free at `_dll_crt0`
startup (`spike/reent-realproc-low-window`, verdict=cleared). Both branches are
gated on the `-DELFSYSV_REALPROC` the crossing-host build alone passes, so the
plain-PE stub the WP-41 exec-* certifications drive keeps its `VirtualAlloc`
reserve and its parent-handover yield unchanged; the exec-* bar
(`loader/exec/t/run.sh`) stays green (10/10) with the branch in place.

Driving the actual bzip2 (`drive.sh /c/-/el8/accept/bzip2/bzip2-1.0.6/bzip2`,
2026-09-02) advances the halt off the low window and onto the next concrete
obstacle. The host now reaches, in order,

    window 0x400000 for 0x3fc00000 held
    runtime elfsysv1.dll at 0x7ffd...
    parsed: 4 PT_LOAD, entry 0x410cee
    exec kind: dynamic

and then faults (SIGSEGV) inside `place` -> `elf_map`, on the first direct
call `elf_map` makes into the faced runtime. The cause is measured, not
inferred: this host compiles `x86_64-pc-cygwin` (default Microsoft x64 ABI,
`__SEH__`), and the faced `elfsysv1.dll` exports System V ABI, so a direct
`elf_map` call to `mmap`/`mprotect`/`write` crosses the DR-0066 Microsoft <->
System V boundary the wrong way and faults -- the same boundary `stub.c` crosses
correctly through the `loader/exec/realproc/` seam (its `say`/`rp_eputs` output
crosses cleanly right up to this point). `stub.c`'s own faced calls go through
the seam; the map/enter path (`elf_map.c`, `host_mem.c`, `dyn_exec.c`,
`dyn_init.c`, `elf_reloc`, `enter.S`) does not, and its bare faced calls are the
live blocker.

## Ordered next step

1. Cross the map/enter path's faced calls the sanctioned way (DR-0066): route
   `elf_map`'s `mmap`/`mprotect`/`munmap` and the rest of the map/enter faced
   surface through `sysv_abi` pointers resolved from `elfsysv1.dll`, the way
   `loader/exec/realproc/realproc-cross.c` already crosses for output, rather
   than as direct Microsoft-ABI calls. Gate it on `ELFSYSV_REALPROC` so the
   WP-41 exec-* bar keeps its native, non-crossing map path. This is the live
   blocker `drive.sh` surfaces today and its own certified increment.
2. With the map/enter path crossing correctly, `elf_map`'s hint `mmap` should
   land bzip2 at its fixed `0x400000` (the low window reads free), and control
   should reach entry with `AT_BASE` carrying the faced runtime's own base so
   the veneer forwarding thunks resolve. Re-run `drive.sh` and read the new
   halt.
3. Then cut `accept.sh`'s `build_loader` onto this host and certify against the
   WP-41 exec-* bar.
