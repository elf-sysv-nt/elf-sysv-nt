
The core's first sit on the substrate leaf: a static program that prints and
exits through the kernel, on substrate N, its behaviour checked against real
Rocky 8. This is proposal 0011 § 18 Phase 1, verification criterion 1, built
under a `/make-it-so` grant on 2026-09-05. `doc/design/Substrate-Interface.md`
is the leaf's contract; `doc/design/Substrate-N.md` is the leaf, now certified;
this is the trunk beginning to rest on it.

## Why this run, and why now

The operator chose it: shown the option of "a running Linux process to point
at", they supplied Rocky 8 under WSL2 — the project's actual el8 target and the
differential oracle 0011's criteria compare against. Substrate H (the other
ready leaf) is deferred, not abandoned; the standard's precedence puts the
operator's explicit steer above this run's default "both leaves before the
trunk". Recorded as D0 below.

Leaf-to-trunk order: substrate N is the certified leaf (`as_map`,
`thread_start`, `user_copy` and the rest, 9/9 conformance). The core is the
trunk, and it may depend only on certified work below it — which the substrate
now is. This increment is the smallest trunk that exercises the leaf end to
end.

## The increment (criterion 1, minimum first, then its remainder)

A static ELF that calls the gate directly runs under the core and prints
`hello`, exiting 0. Concretely, the pieces and their dependency order:

1. **The gate** — the syscall entry the userland reaches instead of the
   `syscall` instruction (N cannot trap `syscall`; spike 1). Number in `%rax`,
   arguments in `%rdi %rsi %rdx %r10 %r8 %r9`, result in `%rax`, `%rcx`/`%r11`
   clobbered; switches to a per-thread kernel stack before anything; its
   address published so a hand-written test program can `call` it directly.
2. **A minimal syscall table** — `write` (fd 1 to the host's stdout through a
   foreign handle) and `exit_group` (exit with the code); every other number
   returns `-ENOSYS`, which is what a 4.18 kernel does for what it lacks.
3. **binfmt_elf** — parse a static `ET_EXEC`/`ET_DYN`, map each `PT_LOAD` onto
   substrate N through `s->as_map` (the certified call), zero `.bss`, find
   `e_entry`.
4. **The initial process image** — the initial stack the x86-64 SysV psABI
   describes: `argc`, `argv`, `envp`, the auxiliary vector (`AT_ENTRY`,
   `AT_PHDR`, `AT_PHNUM`, `AT_PAGESZ`, `AT_RANDOM`, terminated by `AT_NULL`),
   entered at `e_entry` through `s->thread_start`.
5. **The test program** — a static ELF, hand-written to `call` the gate
   directly (not the `syscall` instruction, which N cannot trap, and not a
   stock el8 binary, which uses it), assembled and linked static with the cross
   toolchain under `$ELFSYSVNT_PREFIX/bin`. It does `write(1, "hello\n", 6)` then
   `exit_group(0)`.

The bar, criterion 1's minimum: the program prints `hello` and exits 0 under
the core. Validated against the oracle: an equivalent program run under Rocky 8
(WSL2) produces the identical stdout and exit code. The remainder of criterion
1 — `/proc/self/maps` listing the program, the stack, the vDSO and nothing
else, and the vDSO itself — is the completion, taken in this run if it fits and
named as the boundary if it does not. It is not faked.

## Layout

The 0011 core is new code; 0011 § 6 records that nothing in `veneer/`,
`loader/` or `runtime/winsup/` (the C-library-boundary design) is used by it.
The substrate lives at `substrate/`; the core goes at `core/`, linking
against the certified `substrate/substrate_n.c`. Promoting the substrate
out of `test/` into a first-class `substrate/` is a reasonable later cleanup,
noted and not blocking this increment.

## Decision log

- **D0 — this run builds the core on N, not substrate H.** Tier 7, on the
  operator's explicit steer (the Rocky 8 signal answering the core-Phase-1
  option). The standard's precedence puts that above this run's default order.
  Substrate H remains ready and is the next natural leaf.

- **D1 — the test program calls the gate directly, hand-written.** Tier 1.
  Criterion 1 says "a static test program calling the gate directly". A stock
  el8 binary uses the `syscall` instruction, which N cannot trap (spike 1); the
  toolchain change that emits `call gate` instead is 0011 Phase 3 and not this
  run. So the program is hand-written to the gate ABI and assembled static,
  which depends on neither the toolchain's syscall/gate state nor a rebuilt
  userland.

- **D2 — Rocky 8 is the behavioural oracle, not a bit-for-bit one.** Tier 1.
  N runs gate-calling code, Rocky 8 runs `syscall` code, so the two binaries
  differ by construction; what is compared is the syscall behaviour — the bytes
  `write` emits and the code `exit_group` returns — against the same logical
  program under Rocky 8. Confirmed available: `wsl -d rocky8` returns `hello`
  and exit 0.

- **D3 -- the syscall path is fixed-width, never `long`.** Tier 1. The host
  compiler is LLP64, so `long` is 32 bits, while the gate pushes 64-bit words
  and the Linux ABI passes 64-bit values. `struct sysframe` declared with
  `long` read every argument as half a register: `a1` came back as the high
  half of `%rax`, `a2` as the low half of `%rdi`, and `write` was handed a
  length of zero. It ran, printed nothing, and exited 0, which is the shape of
  this defect -- no warning, no crash, a clean green nothing. The frame, the
  dispatcher's return and the host write are `int64_t` now. The same reasoning
  covers a negative errno, which as a 32-bit return would reach userland
  zero-extended and read as a large positive count.

- **D4 -- the oracle runs the twin of the test program, not a stand-in.**
  Tier 1. The comparison was written as `wsl -d rocky8 -- /bin/echo hello`,
  which prints `hello` whatever the ELF under test contains, so it could not
  fail and was not a check. `test/core/hello-oracle.S` is the twin: the same
  two syscalls with the same arguments, differing only in reaching the kernel
  through the `syscall` instruction and in having no auxv walk, since there is
  no gate address to find. The cross toolchain targets Linux, so what it emits
  runs on el8 unmodified. D2 said the oracle is behavioural rather than
  bit-for-bit; this is what that has to mean in practice.

- **D5 -- the harness hands the core a path its own runtime understands.**
  Tier 1. `lk-host` is a native Windows binary and cannot open the POSIX path
  the shell writes; the run failed at `fopen`. `run.sh` converts through
  `cygpath` where it exists and leaves the path alone where it does not, so
  the script still works from a plain Linux shell once the core builds there.

- **D6 -- check-substrate-line walks `core/` by default, and gates.** Tier 2.
  Its registry note said it was no-op-clean until the core existed and would
  promote then. Called with no arguments it examined nothing and passed, which
  is a green that reports the absence of a check rather than the absence of a
  leak. It now defaults to the core's sources, an explicit path still wins,
  and it is registered at the gate tier.

## The result

Criterion 1's minimum is met. A static ELF, hand-written to the gate ABI and
built by the cross toolchain, runs on substrate N under `lk-host`, prints
`hello`, and exits 0; the twin program on the Rocky 8 oracle prints the same
bytes and returns the same code. `core/run.sh` is the check, registered in
`test/suites.tsv`, and every half of its bar has been watched to fail: the
wrong message, a nonzero exit, and a diverging oracle each turn it red, and
restoring each turns it green.

The remainder of criterion 1 is not met and is not faked. `/proc/self/maps`
listing the program, the stack and the vDSO, and the vDSO itself, are
untouched: there is no `/proc` and no vDSO in this increment. That is the
boundary the plan said to name if it did not fit.
