# The initial process image (WP-40)

The kernel builds a stack before it jumps to a program's `_start`, and glibc
reads that stack back with pointer arithmetic tuned to the exact shape the
kernel laid. There is no kernel here, so the runtime builds it. This package is
that builder: it turns a mapped image, an argument vector, an environment, and
a description of the platform into the block a System V AMD64 program is
entered on, with `%rsp` pointing at `argc`.

`proc_build_stack` is pure layout over a caller-owned buffer. It makes no host
call and reads no ambient state; the identity the auxv reports arrives through
`proc_image_params`, which the runtime fills from the host at exec time
(WP-41) and a test fills with fixtures. The only values it computes for itself
are the ones it can read from the placed image: `AT_PHDR`, `AT_PHENT`,
`AT_PHNUM`, and `AT_ENTRY`. Keeping the ambient reads out of the builder is
what lets the same code be certified against a real Linux auxv without a host
in the loop.

## The layout

From the top of the buffer downward: the pointer targets first — the argument
strings, the environment strings, the `AT_PLATFORM` string, the `AT_EXECFN`
string, and the sixteen `AT_RANDOM` bytes — so the vector below can point up
into them. Then, after alignment padding, the vector itself: `AT_NULL`, the
auxv, the `envp` terminator and the `envp` pointers, the `argv` terminator and
the `argv` pointers, and `argc` at the lowest written word. The entry `%rsp`
is that `argc` word, and it is 16-byte aligned.

The alignment is the one subtlety. The psABI requires `%rsp` to be a multiple
of sixteen at `_start`, and whether it lands there depends on the parity of the
vector's word count. When the count is odd the builder leaves one pad word
between the vector and the strings so `argc` still falls on the boundary. The
unit test sweeps every parity rather than sampling one, because getting this
wrong is silent until a consumer that assumed the alignment does an aligned
load and faults.

`%rdx` at entry carries the shared-object termination handler the runtime wants
registered, or zero. It is not part of the stack; the builder echoes the value
back for the entry trampoline to place, and the entry contract is asserted
there.

## The auxv, and where it may differ from Linux

The auxv has to describe our world honestly enough for a dynamic linker to
initialize against it, and it is emitted in the kernel's own relative order for
the entries carried: `AT_HWCAP`, `AT_PAGESZ`, `AT_CLKTCK`, `AT_PHDR`,
`AT_PHENT`, `AT_PHNUM`, `AT_BASE`, `AT_FLAGS`, `AT_ENTRY`, the four id entries,
`AT_SECURE`, `AT_RANDOM`, `AT_HWCAP2`, `AT_EXECFN`, `AT_PLATFORM`, `AT_NULL`.

`AT_SYSINFO_EHDR` is deliberately absent. There is no vDSO here, and everything
that would have gone through one goes through the runtime instead. A consumer
that treats the missing entry as fatal is a bug this package exists to surface
early rather than in month nine, and the differential reports its absence as an
expected difference rather than a failure so that guarantee is visible in the
transcript.

`t/differential.sh` holds the auxv the builder produces against one a real
Linux kernel builds, captured from `/proc/self/auxv` by `t/dump_auxv.py` under
a real Linux. It compares the set of `a_type` keys, not the values: addresses,
ids, and the host's hwcap differ legitimately between any two machines. The bar
is that every key describing the image or the platform is present on both
sides, that a key present on Linux but not in ours is one this project
deliberately omits or one a newer kernel added past the target world, and that
we invent no key a consumer has never seen. Against a current WSL kernel the
allowed differences are `AT_SYSINFO_EHDR` (no vDSO), `AT_MINSIGSTKSZ`, and the
two `AT_RSEQ_*` entries (kernel facilities newer than el8), and nothing else.

### AT_PAGESZ

`AT_PAGESZ` is reported as 4096, the size Windows commits at, not the 64 KB it
reserves at. The two disagree and only one can be reported; the reasoning for
reporting the commit size is recorded in
`doc/decisions/0014-at-pagesz-commit-granularity.md`. The value is not baked
in — it comes through `proc_image_params.page_size` — but 4096 is what the
runtime passes and what the differential checks.

## Tests

`t/run.sh` certifies the package. The unit tests (`t/unit.c`) check the layout
as arithmetic: alignment across every parity, the two array terminators,
`argc` at `(%rsp)`, the auxv terminator, and the three refusals. The image test
(`t/image_test.c`) drives the whole path — parse (WP-31), map (WP-32), build
(WP-40), enter through the trampoline (`t/enter.S`) — and reads `argc`,
`argv[0]`, `envp`, and the auxv back off the stack the entry specimen
(`t/specimen.c`) was handed, asserting the 16-byte alignment and the `%rdx`
contract as the specimen saw them, and confirming the crossing preserved the
Microsoft-ABI callee-saved registers. The differential runs last.

The entry specimen is a static ELF built by the cross toolchain and entered on
the real psABI stack, not the register convention WP-32's specimen used. It
finds its handshake block through an environment variable the driver set, which
keeps the auxv the builder produced exactly the production set with nothing
injected for the test, and makes finding the block a proof that `envp` was laid
correctly. It returns by restoring a stack pointer the trampoline parked, since
there is no exit to call.

The Linux reference is recaptured when a Linux is reachable and read from the
committed transcript otherwise, so the differential runs on a machine with no
Linux at hand as well as on one with WSL.

## What this does not do

It does not dispatch `execve`, reserve the address space, or opt the host stub
out of CET and Control Flow Guard; those are WP-41, along with filling
`proc_image_params` from the real host and enforcing the reservation-first
ordering spike 2 found. It does not build a stack for `fork` to replay (WP-42)
or lay a signal frame (WP-43). It builds one stack, once, for one image, and
proves the shape is the kernel's.
