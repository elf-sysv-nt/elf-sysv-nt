# gcc, stage one

Like binutils, almost no port. The triple's os and abi fields are honestly
`linux-gnu`, so `config.gcc` already routes it through the ordinary x86_64
Linux arm and the compiler builds without a patch at all.

What it will not do without a patch is carry the two things the target
mandates rather than suggests, and both are values a package can forget to
pass. `patches/0001` adds them.

    build-gcc -P <prefix>
    t/accept.sh -P <prefix>

## The two mandates

`-mno-red-zone`, defaulted through `TARGET_SUBTARGET_DEFAULT` rather than
through a spec string. The order of that sentence matters and is easy to read
backwards, so it is worth stating the destination before the mechanism: this
platform is meant to honour the red zone, and does not yet.

The psABI reserves 128 bytes below `%rsp`, a conforming platform leaves them
alone, and the direction is to honour them at the delivery site the way a
Linux kernel does rather than to compile the world with a flag that announces
in every leaf's prologue that this is not quite the ABI it claims to be. WP-43
is where that lands. The flag here is scaffolding for the bootstrap.

What stands in the way is not Windows. Spike 3 measured the host leaving the
reserved bytes alone under preemption, thread hijacking and its own exception
dispatch, and measured Cygwin's own delivery taking the word at `%rsp-8` on
every single delivery. So the two repairs are not alternatives to pick
between; they are steps in an order. Until delivery reserves the bytes before
it builds a handler frame, an object compiled with a red zone corrupts a stack
at an unpredictable later date in a package nobody was looking at, and
removing the flag first would not make this platform more faithful to ELF — it
would make every leaf a defect.

`-mred-zone` still turns it back on, and that escape hatch is the point rather
than a concession. Spike 7 showed a delivery that reserves the 128 bytes first
keeps them whole; WP-43 has to price that against Cygwin's real `sigdelayed`
and write the record that retires the flag. A target that refused the option
outright would force a toolchain rebuild before that measurement could be
taken at all.

`AGENTS.md` reserves the choice between the two repairs and nothing here makes
it. Hand-written assembly is reached by neither, which is what
`bin/asm-ledger` exists for.

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
nothing. It now asks the compiler what it believes its default is, and
separately compiles a leaf that must spill, with the `-mred-zone` case as the
negative control.

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
