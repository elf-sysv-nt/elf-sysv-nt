# glibc for substrate N

el8's glibc 2.28, rebuilt so that it reaches the kernel through the gate and
finds its thread pointer through `%gs`. This is the toolchain half of phase
3 (0011 § 18): the library every rebuilt package links against, and the
`ld.so` the kernel jumps to for a dynamic image.

    build-glibc
    t/accept.sh

`build-glibc` fetches el8's source package by checksum, prepares the tree
the way the spec file's `%prep` does, applies `patches/`, configures a cross
build against the kernel headers already in the sysroot, builds, stages the
install, runs `check-no-syscall` over every staged object, and only then
copies the result into the sysroot at
`$ELFSYSVNT_PREFIX/x86_64-elfsysvnt-linux-gnu/sys-root`, the sysroot spelling
`doc/design/target-definition.md` fixes. `t/accept.sh` is
the bar. `check-no-syscall` is the post-link check 0011 § 16 asks for, and
it is what the rpm macros will run over every package.

## What the port changes, and why so little

Nothing above `sysdeps/` is touched: `printf`, `malloc`, the resolver, NSS,
locales and the dynamic loader are upstream's, patched as el8 patched them
(1096 patches, applied in the spec's order). The port is one patch over
that, `patches/0001`, and it changes two things about how the x86-64
`sysdeps` talk to the machine.

The kernel is entered by a `call`. Substrate N cannot trap the `syscall`
instruction, so the kernel publishes a gate address in the auxiliary vector
as `AT_SYSINFO` and user code calls it under the Linux register convention:
number in `%rax`, arguments in `%rdi %rsi %rdx %r10 %r8 %r9`, result in
`%rax`, `%rcx` and `%r11` clobbered and everything else preserved. The
address is kept in `GLRO(dl_sysinfo)`, exactly where the i386 port keeps
its vsyscall entry (`NEED_DL_SYSINFO`; `_dl_sysdep_start` and `_dl_aux_init`
already store `AT_SYSINFO` there), and every caller reaches it the way its
object reaches any other `GLRO` word: `_rtld_local_ro` inside `ld.so`, the
GOT from every other shared object, `_dl_sysinfo` in a static link. The TCB
carries a copy too, as i386's `sysinfo` does, but nothing enters through
it: `ld.so` maps and protects segments before it has a thread pointer, and
a static program allocates its TLS with `brk` before it has one. A call
pushes a return address below `%rsp`, so the C form steps over the red zone
first, the way `test/core/lksys.h` does; of the hand-written sites only
`____longjmp_chk.S` keeps data there, and it steps over too.

The thread pointer is a load from the TEB. `%gs:0x1678` is `TlsSlots[63]`
(DR-0101), and the two slots below it hold what the psABI keeps at fixed
offsets from the thread pointer: the stack-protector canary at `%gs:0x1670`
and the pointer guard at `%gs:0x1668`, so that `-fstack-protector` and
`PTR_MANGLE` stay one instruction each. `TLS_INIT_TP` is a store;
`arch_prctl` is never called. `THREAD_SELF` loads the slot, and the
`THREAD_GETMEM` family becomes an ordinary access through the descriptor,
which is the shape aarch64 and every other port without a segment base
already has. The `tcbhead_t` copies of the two guards are still written,
because a new thread's TEB slots start empty: `clone.S` copies them out of
the TCB, where the creating thread put them, before the first protected
frame runs on the new thread. The kernel's obligations are the slot itself,
written at `exec` and at `clone` with `CLONE_SETTLS`, and the caller's slot
value when `CLONE_SETTLS` is absent, which is Linux's inheritance of the FS
base.

Two things are replaced rather than translated. The x86-64
`cancellation.S` kept to a convention that left no register for the
descriptor, so `nptl/`'s C versions serve instead; they were the
specification the assembler was written to. The `catomic_*` forms in
`atomic-machine.h`, which tested the TCB's `multiple_threads` word to skip
the lock prefix in a single-threaded process, lock unconditionally, which
is what `include/atomic.h` does for a port that defines none; `malloc` and
`dl-profile` are the users, and the cost is a locked instruction on a path
that is a few hundred cycles long.

The three slot constants are defined once, in `sysdeps/x86_64/nptl/tls.h`,
for C and assembler alike, and change together with the substrate's
(`doc/design/Substrate-N.md`) or not at all.

## The toolchain it needs

Three pieces of the toolchain changed with this port, and glibc does not
build without them.

`toolchain/gcc/patches/0002` makes the compiler load the thread pointer
from `%gs:0x1678`, read the canary from `%gs:0x1670`, default
`-mno-tls-direct-seg-refs` and refuse the option, and withdraw
`-fsplit-stack`, whose guard word was `%fs:0x70` in libgcc's hand-written
`__morestack`. `toolchain/binutils/patches/0002` makes the linker's TLS
relaxations write `mov %gs:0x1678,%rax` where they wrote `mov %fs:0,%rax`
(the same nine bytes), and drops the refusal patch 0001 carried, which had
also refused `GOTTPOFF` and with it every static link against a glibc
`libc.a`. Both are rebuilt with the scripts beside them, in that order:
`build-binutils`, then `build-gcc` (stage one, C only), then this, then
`gcc/stage2/build-gcc2` for `libgcc_s` and `libstdc++` against the new
library.

## The bar

Fixed before the port was written, in `t/accept.sh`, which exits 0 iff the
first three hold:

1. glibc builds for the target and installs, and `check-no-syscall` passes
   over every installed object: no `syscall` instruction, no `%fs` access.
2. A static C program using `printf` (`t/hello.c`) links against the result,
   and its disassembly is clean by the same check.
3. That program runs under `lk-host`, prints `hello`, and exits 0.
4. (stretch) The same program linked dynamically runs the same way, through
   the rebuilt `ld.so`.

Claim 3 rests on the kernel implementing what glibc's static startup and
stdio need; where it fails only because a syscall returns `ENOSYS`, the
numbers are recorded below rather than papered over.

## Status

Filled in as the certification runs; see the decision log and the report
at the end of this file.

## The veneer is gone

`toolchain/sysroot/install-veneer` once put a `libc.so.6` face over Cygwin
into this sysroot. The veneer arc is retired (DR-0097), `build-glibc`
sweeps that face on its first run, and nothing keeps it: the sysroot's
`usr/lib64` and the glibc-owned part of `usr/include` are this script's,
recorded in `.glibc-manifest` so that a later run removes exactly what it
installed. The kernel's uapi headers beside them are
`toolchain/sysroot/kernel-headers`' and are left alone.

## Decision log

Each entry names the tier of `doc/design/decision-ladder.md` that
discriminated.

**The gate address is reached through `GLRO`, not the TCB.** Tier 1. The
TCB copy is unavailable in `ld.so` before `TLS_INIT_TP` and in a static
program before `__libc_setup_tls`, and both make system calls there
(`mprotect` in `_dl_map_segments`, `brk` in `__libc_setup_tls`). i386
resolves the same problem with `_dl_sysinfo` and `int $0x80`; there is no
fallback instruction here, so `GLRO` is the only word that is always set
before it is needed. The default is a stub that faults at a named symbol
(`_dl_sysinfo_nogate`), for diagnosability.

**One patch for the port, not one per concern.** Tier 7. The gate and the
thread pointer share files (`sysdep.h`, `clone.S`, `sysdep.S`), and a
split would put half-ported files in the first patch. Build fixes are
separate patches, one per cause.

**`patches/0002` is a make fix, not a port.** Tier 1, measured. Under GNU
make 4.4.1 the build never left `stdio-common`: `bits/stdio_lim.h` and its
`.d` file shared a pattern rule with a no-op recipe, make 4.3 and later
treat a pattern rule's targets as a group, and remaking the header (every
pass, since the stamp beside it is newer) marked the included `.d` remade
too, so make re-read everything and did the same again. `make -d` on the
subdirectory showed twenty-five re-executions in twenty seconds; a rule of
its own for the `.d` file brought it to one. el8 built this tree with make
4.2, which did not group them. The first diagnosis, of a race in the
gen-as-const rule, was wrong: two builds of mine had been running in the
same tree at once, and the "race" did not reproduce alone, so that change
was withdrawn before it was committed.

**The build tree is case-sensitive, and the script makes it so.** Tier 1.
glibc compiles `foo.os` for the shared library and `foo.oS` for the static
PIC archive from one source, side by side, and keeps stamps named
`stamp.os` and `stamp.oS`; on a case-insensitive directory each pair is one
file, the objects overwrite each other, and the stamps' temporaries
collide under `-j`, which is how it surfaced (`mv: cannot stat
stamp.oST`, in `nptl`, three runs out of three). Windows gives a
directory case sensitivity of its own, inherited by everything created
beneath it, so `build-glibc` marks an empty build tree with `fsutil` before
the first object lands and refuses a populated tree that is not marked. The
source tree and the staged install carry no such pairs and are left alone.

**`cancellation.S` is replaced by `nptl/cancellation.c`.** Tier 1 with a
tier-4 tie-break. The assembler's contract was to clobber only `%rax` and
`%r11`, and a descriptor access needs a third register; the C version is
what every other port runs and what the assembler was written against.

**`catomic_*` lock unconditionally.** Tier 4 over tier 6. The generic
fallback is what a port without the optimisation gets; keeping the
optimisation would mean a thread-pointer load in nine asm bodies for a
saving measured in tens of cycles on `malloc`'s statistics counters.

**`vfork` keeps its syscall number.** Tier 1. 0011 § 5 has the kernel
implement `vfork` as a fork the parent waits on; a wrapper that issued
`fork` would lose the wait that `posix_spawn`'s callers rely on. The
`spawni.c` change 0011 § 5 also names (the pipe-based error report, since
the child's memory is a copy under N) is deferred until the kernel has
`fork`; nothing here can exercise it.

**`--disable-static-pie`.** Tier 1, deferred rather than decided. A static
PIE relocates itself, `mprotect`s its RELRO segment and only then reads the
auxiliary vector, so its first system call has no gate address; i386
survives on `int $0x80`. el8 enables it and ships almost nothing linked
that way. Enabling it means `_dl_relocate_static_pie` reading `AT_SYSINFO`
off the initial stack before relocation, which is a small change in
`elf/dl-reloc-static-pie.c` for whoever needs it.

**`--enable-cet` is dropped; the CET paths keep their `%fs`.** Tier 1. The
toolchain opts out of CET (DR-0062), `SHSTK_ENABLED` is 0 without
`__CET__`, and the guarded code (`feature_1`, `ssp_base` through `%fs` in
`setjmp.S`, `__longjmp.S`, the `*context.S` trio, `____longjmp_chk.S`) is
not compiled. It is left as upstream wrote it rather than ported blind.

**The binutils refusal became a rewrite.** Tier 1, recorded in
`toolchain/binutils/README.md`. DR-0063's "linker half" paragraph and
DR-0024's descriptor note describe the refusal; they are reported for
amendment rather than edited here, since the design documents are not
this unit's to change.

**Where the three constants live.** Tier 5. In `tls.h`, once, with names
that say what they are, rather than as literals at each site; a reader
grepping for `0x1678` finds one definition and its readers.

## Not verified

That `libthread_db` finds a thread. `DB_THREAD_SELF` still names the FS
register, which under N holds nothing; a debugger's thread list is the
first thing that will notice, and the fix is a `td_ta_map_lwp2thr` that
reads the TEB slot, which needs `ptrace` first.

That anything with more than one thread works. Claim 3 is a single thread;
`clone.S`'s slot copies and the kernel's `CLONE_SETTLS` handling are read
against the design and not run.

That the `vDSO` symbols glibc looks up (`__vdso_clock_gettime`,
`__vdso_gettimeofday`, `__vdso_time`, `__vdso_getcpu`) are what the
kernel's `vDSO` exports. glibc falls back to the system call when a symbol
is absent, so a missing one costs time and not correctness.

The `posix_spawn` path, `fork`, signals and everything else the kernel does
not yet implement. The bar is a static `hello`.
