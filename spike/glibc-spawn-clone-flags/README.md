# What glibc 2.28 asks the kernel for in `posix_spawn` and `vfork`

Which `clone` flags does el8's `posix_spawn` use, and what syscall is its
`vfork`? Read out of the shipped `libc-2.28.so`, not out of memory.

`posix_spawn` and `posix_spawnp` both reach `__spawnix`, which calls `__clone`
with `0x4111`: `CLONE_VM | CLONE_VFORK | SIGCHLD`, the child running
`__spawni_child` on the parent's memory. `__vfork` pops its return address,
issues syscall 58 (`vfork`) and pushes it back. `results-2026-09-05.txt` is the
transcript.

## Why it matters

Proposal 0011 § 6 says that `clone` "with `CLONE_VFORK` but without
`CLONE_VM` is the `vfork` glibc's `posix_spawn` uses", implements that as fork
plus wait, and returns `EINVAL` for every other flag combination. If the
shipped libc uses `CLONE_VM`, then under substrate H, which runs that libc
unmodified, `posix_spawn` gets `EINVAL` and every caller of it fails; and
`__spawni_child` reports an `exec` failure by writing `args->err` in the
parent's memory, which only means anything if the memory is shared. Under N
the `sysdeps` port has to know what it is patching. The review of 0011 raised
this from memory of the glibc source; proposal 0012 needed it as a fact.

**Gates.** 0012's `vfork` and `CLONE_VM` design, and the file list of N's
`sysdeps` port.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Needs the cross binutils' `objdump` under `$ELFSYSVNT_PREFIX/bin` and the
vendor `libc-2.28.so` that `spike/vendor-image-shape/` stages from the pinned
`glibc-2.28-251.el8_10.40` RPM under `$ELFSYSVNT_EL8/vendor-image-shape/ref/`;
`-L` points it at another copy. Nothing runs under a Linux kernel and nothing
is installed anywhere; it reads one file.

## Method

`objdump -d` over the whole library, then each function's body cut from its
label to the next. The path is followed by the calls the disassembly names:
`posix_spawn@@GLIBC_2.15` calls `__spawni`, `__spawni` reaches `__spawnix`,
`__spawnix` calls `__clone`. The flags are the last immediate moved into
`%edx` (the third argument) before that call, decoded against `CLONE_VM`
(`0x100`), `CLONE_VFORK` (`0x4000`) and the exit-signal byte. `__vfork`'s
syscall number is the last immediate moved into `%eax` before its `syscall`.
The transcript carries both instruction sequences so the reading can be
checked by eye.

## What this does not reach

Only the shipped x86-64 binary was read; the source's `spawni.c` was not
opened, and Red Hat's patches to it are not distinguished from upstream's.
Whether `system()` and `popen()` in 2.28 go through `posix_spawn` or `fork`
was not measured (upstream moved them in 2.29). One libc build, one
architecture.
