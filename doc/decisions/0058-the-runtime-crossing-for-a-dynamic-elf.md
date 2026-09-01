# DR-0058 — The runtime crossing for a dynamic ELF

DR-0045 settled how a static ELF reaches the faced runtime: the stub loads
the runtime, the image finds it through `AT_BASE`, and a hand-written call
walks from there to a real export. bzip2 is not that image. It is an
`ET_EXEC` that names `/lib64/ld-linux-x86-64.so.2` in a `PT_INTERP` and
imports forty libc symbols from `libc.so.6`, and its `e_entry` is a `_start`
that reads those symbols through an unrelocated GOT. Entered the way the stub
enters a static image, it faults on its first library call. This record
settles how the dynamic image crosses instead, over the loader packages
already delivered rather than any new one.

## Who is the interpreter

The runtime. Linux would map `ld-linux` beside the image and enter it, and
that interpreter would map `libc.so.6`, relocate the program against it, run
the initializers, and jump to `e_entry`. Here the faced runtime is both the
interpreter-shaped party DR-0045 already loads at `AT_BASE` and the object
that exports `libc.so.6`'s symbols. So `PT_INTERP` is satisfied by the
runtime that is already loaded, not by mapping a second ELF: the name
`/lib64/ld-linux-x86-64.so.2` is honoured by identity of role, and the name
`libc.so.6` in the image's `DT_NEEDED` resolves to that same already-present
object rather than to a file opened from disk. There is no separate
interpreter image to place, and `AT_BASE` keeps the meaning DR-0045 gave it.

## What does the linking

The loader packages, unchanged. WP-33 already walks a dependency closure and
seeds initialization order; WP-34 relocates; WP-35 resolves a symbol against
a scope; WP-36 decides the version a `verneed` entry demands; WP-38 holds the
object table those hang off; WP-39 announces the result to a debugger through
`r_debug`. The dynamic crossing is a driver that composes them for the one
closure a program start presents -- the main image and the runtime-as-libc --
rather than a new implementation of any of them. It enters the main image
into WP-38's table as the load-order root, enters the runtime as its single
satisfied `DT_NEEDED`, relocates the main image's GOT and PLT against the
runtime's exports through WP-34 and WP-35 with WP-36 deciding each versioned
symbol, runs the image's `DT_INIT` and `DT_INIT_ARRAY` in WP-33's order, and
only then makes the crossing the stub already owns.

## Where it sits

Between the map and the entry, chosen by the classifier. WP-56's
`exec_kind_of` reads the parsed image and returns `static`, `dynamic`, or
`unsupported`; the stub keeps its present path for `static` -- the one WP-41
certifies, entered at `e_entry` with no loader -- and for `dynamic` runs this
driver against the mapped image before `elf_enter`. `unsupported` is refused
rather than entered: a relocatable object, a core, or a bare shared object is
not a program on this route. The split is the classifier's single decision,
so the stub gains one branch, not a second parse.

## What is not settled here, and what certifies it

The bias for an `ET_DYN` program image is the driver's to compute and is left
to the driver's own record; this one fixes the roles and the order, not the
arithmetic. The certification is bzip2 itself: cross-built to its el8 ELF,
run through the stub with the runtime supplied, its `make test` executed and
passed. That green is WP-56's overall done-when, and it is the driver's to
reach; the classifier this record leans on is landed and certified, and the
driver is what the next slices build on it.
