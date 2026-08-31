# gcc, stage one

Like binutils, almost no port. The triple's os and abi fields are honestly
`linux-gnu`, so `config.gcc` already routes it through the ordinary x86_64
Linux arm and the compiler builds without a patch at all.

What it will not do without a patch is carry the two things the target
mandates rather than suggests, and both are values a package can forget to
pass. `patches/0001` adds them.

    build-gcc -P <prefix>
    t/accept.sh -P <prefix>

## The mandate, and a retirement

The target header mandates one thing now, `__ELFSYSVNT__`. It once mandated a
second, `-mno-red-zone`, and that one is retired.

The red zone is the psABI's: the 128 bytes below `%rsp` a conforming leaf uses
and a conforming platform leaves alone. DR-0006 chose to honour them at the
delivery site, the way a Linux kernel does, rather than to compile the world
with a flag that announces in every leaf's prologue that this is not quite the
ABI it claims to be, and it carried `-mno-red-zone` only as bootstrap
scaffolding. WP-43 built and certified the delivery repair -- Cygwin's signal
delivery reserves the 128 bytes before it builds a handler frame -- and the
reservation's cost measured negligible. So the flag is retired: the subtarget
default no longer disables the red zone, a leaf uses it as System V code does,
and the delivery repair keeps it whole for compiled and hand-written code
alike. `-mred-zone` and `-mno-red-zone` stay selectable as on any x86-64
target; neither is forced. The decision superseding DR-0006 records it, and
`bin/asm-ledger`'s WP-16 ledger of the hand-written residue the flag never
reached closes with the flag.

`__ELFSYSVNT__`, because WP-11 taught `config.guess` to ask the compiler which
vendor it is building for. `uname` cannot answer: `sysname` is `Linux` here by
design, so that the configure scripts branching on it keep working. Without
this define a native build silently configures as `x86_64-pc-linux-gnu`, which
is the failure DR-0001 chose the vendor field to avoid.

Nothing else needed touching. `GLIBC_DYNAMIC_LINKER64` in `i386/linux64.h` is
already `/lib64/ld-linux-x86-64.so.2`, which is what `doc/target-definition.md`
fixes the loader SONAME at, so the two agree without a patch and `t/accept.sh`
checks that they keep agreeing.

## The mistake worth keeping

Defaulting the red zone means writing `TARGET_SUBTARGET_DEFAULT`, and
`i386/unix.h` already keeps three flags there: `MASK_80387`, `MASK_IEEE_FP`
and `MASK_FLOAT_RETURNS`. The first version of this patch assigned
`MASK_NO_RED_ZONE` instead of ORing it in, which turned the x87 off.

Nothing complained. Configure succeeded, the compiler built, it compiled
freestanding objects, and it reported `-mno-red-zone [enabled]` exactly as
wanted. The failure surfaced twenty minutes later in libgcc, building
`__mulxc3`, as `x87 register return with x87 disabled` — an error that reads
like a libgcc bug and is not one. `t/accept.sh` compiles a `long double`
function for that reason alone.

The red-zone claim is checked two ways for a related reason. A first version
checked codegen only, against a fixture the optimiser could keep in registers,
so it compiled identically with and without the flag and passed while proving
nothing. It now asks the compiler what it believes its default is -- the red
zone, since the flag is retired -- and separately compiles a leaf that must
spill, confirming it uses the red zone by default with the `-mno-red-zone` case
as the negative control that makes room instead.

## Not verified

That stage one is enough for anything but WP-14. It has no libc, no threads,
no shared libraries and only C, which is what the bootstrap's first turn is
supposed to be. WP-15 is the second turn and it waits on a libc.

That the prerequisites are pinned. `contrib/download_prerequisites` verifies
gmp, mpfr, mpc and isl against checksums it carries itself, so a pin exists,
but it is upstream's rather than ours and `gcc.pin` does not record it.

That 13.3.0 is the right release. It was chosen over el8's 8.5 for the newer
x86 support, on the same reasoning as binutils 2.42, and nothing has yet
needed either.

That the compiler emits anything sane for TLS. It has not been asked to. WP-12
now refuses the relocations the psABI's TLS models generate, so a program
using `__thread` will fail to link rather than link wrongly, and what this
compiler should emit instead is WP-30's to settle.
