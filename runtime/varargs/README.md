# The variadic surface (WP-24)

Every variadic export of `elfsysv1.dll` -- the `printf` family, the `scanf`
family, and the two dozen other functions shaped like them -- is a place where
a System V caller reaches a Microsoft-ABI core, and where the call cannot be
forwarded. This package is the deliberate handling that seam needs: the reason
no forward exists, the one pattern that replaces it, the generator that writes
that pattern out, and the enumeration of which exports it applies to.

## Why a variadic call cannot be forwarded

Spike 3 measured it. On this target gcc's default `va_list` is the Microsoft
one -- an eight-byte pointer walked in place -- and the System V one is a
twenty-four-byte descriptor reached only through `__builtin_sysv_va_list`:

    struct { unsigned gp_offset, fp_offset; void *overflow_arg_area, *reg_save_area; }

The two are not different encodings of the same thing. A System V variadic
argument lives in one of two register files, integer or floating, and the
descriptor's two offsets say which; a Microsoft one lives in a single flat run
of eight-byte stack slots. Hand a System V list to a reader shaped for
Microsoft's and the first thing it fetches is the descriptor's header read as
an integer -- the spike saw 206158430216 where 111 was passed. So the boundary
cannot be crossed by handing the list down. `printf` cannot be a tail call, the
way a fixed-arity down-call is in WP-21.

There is a second, quieter reason a forward is impossible, and it decides the
shape of the fix. Even having walked the System V list, C offers no way to
splat a run-time-determined sequence of values into a `...` call: the argument
list of a variadic call is fixed at compile time. So the repass cannot target a
`...` callee. It targets the core's `va_list`-taking form -- `vfprintf`,
`vsnprintf`, `vfscanf` -- and to do that it must first **build** a Microsoft
`va_list`. The whole package turns on that: walk the System V list, build a
Microsoft one, call the `va_list` core. DR-0015 records the decision.

## The unpack-and-repass pattern

Two shapes cover the surface.

**Format-driven** (the `printf` and `scanf` families, and the `err`/`warn`,
`syslog`, `error`, and `setproctitle` reporters -- everything that carries a
format string). The format is the only thing that recovers, for each argument,
whether it came from the integer register file or the floating one; nothing
generic can read that back from the bytes. So `sv2ms.c` walks the format, pulls
each argument from the System V list with its true type -- which is what tells
`__builtin_va_arg` which save area to take it from -- and lays it into a flat
array of eight-byte slots, which is exactly a Microsoft `va_list`. Every scalar
a conversion consumes (an `int`, a `long`, a `long long`, a `double`, any
pointer) occupies one slot, so the rebuild is a copy of values, not a
re-encoding. A generated entry point does three things:

    __attribute__((sysv_abi))
    int printf(const char *__fmt, ...)
    {
        unsigned long long __slots[VARARGS_MAX_SLOTS];
        __sysv_va_list __ap;
        __sysv_va_start(__ap, __fmt);
        va_list __ms = __sv2ms_print(__slots, VARARGS_MAX_SLOTS, __fmt, __ap);
        int __ret = __core_vfprintf(stdout, __fmt, __ms);
        __sysv_va_end(__ap);
        return __ret;
    }

The `v`-forms (`vfprintf`, `vsnprintf`, ...) are the same but for receiving the
System V list as a parameter rather than starting one; on the System V side
their `va_list` argument *is* a System V `va_list`, which is why they need the
rebuild as much as the `...` forms do.

**Prototype-driven** (the exec and spawn families, `open` and `openat`, `fcntl`
and `ioctl`, the SysV IPC ctls, `mq_open`, `sem_open`, `cygwin_internal`).
These carry no format, but each has a fixed, known signature, so its trailing
arguments are walked one at a time for the exact types the prototype names and
passed to a fixed-arity Microsoft-ABI core -- no `va_list` rebuild, the
System V to Microsoft shuffle emitted by the compiler at the call site as in
WP-21. `nonformat.c` works two of them, `open` and `execl`, as the pattern for
the rest.

Both shapes name only the Microsoft-ABI **core**, never a public export, so a
`printf` wrapper cannot recurse into itself. `core.h` is that contract -- the
`va_list`-taking, Microsoft-ABI back ends WP-22 provides out of the re-faced
runtime. The test supplies a stand-in (`t/core.c`) that forwards each to the
host's own formatter, which is the "model the MS-ABI callee with the runtime's
`snprintf`" the done-condition allows.

## The bridge

`sv2ms.c` holds the four rebuild functions -- print and scan, narrow and wide.
The print walk handles the flag, width, precision (including `*`, which
consumes an `int` each), and length-modifier syntax, and pulls each conversion's
argument at the width the length modifier sets. The scan walk lays out one
pointer slot per non-suppressed conversion, since every `scanf` argument is a
pointer. `VARARGS_MAX_SLOTS` bounds a call at 128 arguments, a one-kilobyte
stack buffer, refused rather than overflowed.

The one scalar the copy cannot carry is `long double`. Its representation
itself differs between the ABIs -- eighty bits in sixteen bytes on the System V
side, sixty-four on the Microsoft one -- so no slot copy converts the value; the
walk pulls the argument to stay aligned and narrows it to `double`, and a
format that needs exact `long double` is outside what a value copy reaches. It
is noted here rather than hidden.

## The variadic export set

Sixty-eight exports, derived and certified by `derive-variadic.sh`. The method,
in a sentence: a function is in the set when its C prototype's last parameter is
an ellipsis -- the format and sentinel variadics -- or a `va_list` -- the
`v`-forms -- and the set is that predicate intersected with the export
inventory. The `.din` WP-20 cut does not mark variadics, so the shape is read
from the signature; `variadic-exports.tsv` is the maintained record of that
reading, and `derive-variadic.sh` certifies it against `../exports/cygwin-exports.tsv`:
every listed name is really an exported function, and the three look-alikes a
loose signature scan sweeps in but that are not in fact variadic are absent --
`__eprintf` (four fixed arguments), `shmctl` and `msgctl` (a trailing struct
pointer, not an ellipsis). With `--headers` it also scans the Cygwin headers at
the runtime base and flags any exported variadic-shaped prototype the record
misses; that scan misses multi-line and macro-built declarations, so it aids
maintenance rather than being the source of record.

    total          68
    format-driven  54    (printf/scanf families, err/warn/syslog/error, setproctitle)
    prototype      14    (exec, spawn, open, openat, fcntl, ioctl, semctl,
                          mq_open, sem_open, cygwin_internal)
    by ellipsis    44
    by va_list     24

`derive-variadic.sh --terse` prints these. The enumeration doubles as the
generator's input: each format-driven row carries its shape and the core it
repasses into.

## The generator

`gen-veneer.sh` reads `variadic-exports.tsv` and writes `veneer.gen.c` and
`veneer.gen.h`, one `sysv_abi` entry point per format-driven export, both
committed and both reproduced byte for byte by a rerun. It is table-driven: a
shape fixes the return type, the fixed parameters before the format, which
rebuild the format drives, and the arguments the core takes; the enumeration
names the shape and the core, and two templates -- one for the `...` forms, one
for the `v`-forms -- do the rest. Prototype-driven rows are listed and skipped,
since each is a distinct signature with no family to fold into and is
hand-written in `nonformat.c`.

The wrappers define the public names (`printf`, `vfprintf`, ...) as `sysv_abi`,
which conflict with the host libc's own Microsoft-ABI declarations of the same
names. In the runtime they are compiled against the veneer's own System V-faced
headers (WP-50), where there is no other declaration to conflict with. To
certify the generated code compiles and runs here, without those headers,
`--prefix` regenerates the identical wrappers under a `v24_` prefix; the test
builds and runs that, so what is exercised is the generator's own output, only
renamed.

## Files

    sv2ms.h / sv2ms.c     the System V to Microsoft va_list rebuild
    core.h                the MS-ABI core contract the veneer repasses into
    variadic-exports.tsv  the enumeration, and the generator's input
    derive-variadic.sh    derive and certify the set against the inventory
    gen-veneer.sh         generate the veneer from the enumeration
    veneer.gen.c/.h       the generated format-driven entry points (committed)
    nonformat.c           the prototype-driven pattern, open and execl worked
    t/core.c              a stand-in core: forwards to the host formatter
    t/varargs-test.c      the done-condition, made runnable
    t/run-tests.sh        build the prefixed veneer and run the test
    t/reproduce.sh        the four-gate certification

## Certifying

    ./derive-variadic.sh --headers    # the set is consistent with the inventory
    ./gen-veneer.sh                   # regenerate the committed veneer
    ./t/run-tests.sh                  # the done-condition
    ./t/reproduce.sh                  # all four gates

`t/reproduce.sh` runs four gates: the set is consistent, the veneer reproduces
byte for byte, the bridge and prototype-driven entries and the generated veneer
all compile clean, and the runnable test passes. The test's four cases are the
`va_list` incompatibility (kept beside the veneer as spike 3's `varargs-raw`
is), a sixteen-argument `printf` of mixed integers and floats, a `vfprintf`
reached through a System V `va_list` with a Microsoft-ABI callee, and a `scanf`
round-trip.

The sixteen-argument reference was taken from glibc, not from this project's own
reading: the same format and arguments printed under glibc 2.35 give

    1 2.500000 3 four 5a Z 7.125 8 9 10 11.500000 twelve 13 1.400000e+03 15 16.000000

and the veneer's `printf`, `vfprintf`, and `snprintf` paths all reproduce it.
The test also cross-checks against the host's own formatter live, so a drift in
either oracle fails it.

## What this does not do

`long double` by exact value, for the representation reason above. Positional
arguments (`%2$d`) and the glibc scanf `%m` allocation modifier are not parsed;
newlib exports neither into this surface, but a format using them would rebuild
wrong rather than be refused, which is the sharp edge to widen the walk for if
the surface ever grows one. The wide families are implemented on the same
bridge and compiled, but the done-condition exercises the narrow ones; a wide
differential against glibc is the test worth adding next. The prototype-driven
fourteen are enumerated and their pattern is worked for `open` and `execl`; the
remaining twelve are the same walk against their own signatures and are WP-22's
to wire into real cores.

## Not verified

That the committed `veneer.gen.c` compiles as-is. It defines the public libc
names as `sysv_abi` and so cannot compile against the host's headers; it is
certified through the `v24_`-prefixed regeneration, which is the same generator
output, and will compile unprefixed only against the veneer's own headers once
WP-22 and WP-51 wire them. The prefixed build is the standing evidence that the
generated code is correct C.

That the reproduce transcript regenerates identically. The generated files and
the derived counts reproduce byte for byte; the test's pass or fail is stable,
but it links the host libc, so its output is a property of this machine's
newlib as much as of the veneer -- which is why the glibc reference is carried
as a constant rather than recomputed here.
