# The object graph (WP-33)

A dynamically linked program does not name its libraries by file. It names them
by soname in `DT_NEEDED`, each named library names more, and before anything can
be relocated the loader has to turn that graph of names into a list of files, in
the order a real `ld.so` would load them. This package is that step. Given a
root ELF object and a search configuration it walks the graph the object roots,
resolves every `DT_NEEDED` name to a file, and returns the load order. It builds
on WP-31 for reading each object's dynamic section and on WP-32's placement not
at all: nothing here maps or relocates, and turning this order into running code
is WP-34 and the packages after it.

The list this produces is the same list, in the same order, that `ldd` prints.
That is not a coincidence to be admired after the fact; it is the specification
the package is written to and the bar it is tested against, object for object,
against a real glibc `ld.so`.

## The order is breadth-first

The walk is breadth-first over `DT_NEEDED`. The root's needed names are resolved
in the order they appear, then each of their needed names, and so on, with each
object entered into the load map the first time it is reached and never again.
An object reachable by two paths — the diamond, where the root needs A and B and
both need D — appears once, at the depth it was first found, after both of its
loaders rather than between them. The implementation is an array walked by a
moving index: expanding the object at the index appends its dependencies past
the end, and the index only reaches them once every object at its own level has
been expanded, which is breadth-first without a separate queue.

An identity is needed to decide whether an object has already been entered, and
that identity is `DT_SONAME`, not the file name and not the `DT_NEEDED` string.
Two names that resolve to one soname are one object; a name with no provider is
still entered, as a node marked not-found, so the order a broken graph produces
matches what `ldd` shows for it rather than silently closing the gap.

## The search, and the one difference that matters

A `DT_NEEDED` name without a slash is resolved against five sources, in this
order:

1. `DT_RPATH` of the loading object and of every object on the chain of loaders
   above it, nearest first;
2. `LD_LIBRARY_PATH`;
3. `DT_RUNPATH` of the loading object only;
4. the `ldconfig` cache;
5. the system default directories.

The first file that exists and is a usable ELF object wins. A name that does
contain a slash skips the search and is used as given, after token expansion.

The difference between `DT_RPATH` and `DT_RUNPATH` is the whole reason both
exist, and it is two differences at once. `DT_RPATH` is searched *before*
`LD_LIBRARY_PATH` and `DT_RUNPATH` *after* it, so which of the two an object
carries decides whether the environment can redirect its dependencies. And
`DT_RPATH` is *inherited* — an object's rpath is on the search path for the
dependencies of its own dependencies, down the chain — while `DT_RUNPATH`
applies only to the object that carries it and reaches no further. The second
difference is the sharper one in practice: a library that finds its plugins
through an rpath keeps finding them one level down; the same library switched to
a runpath does not, and the failure surfaces only in the transitive case.

Both differences fall out of one rule the loader follows when it reads an
object: `DT_RPATH` is ignored entirely on any object that also carries
`DT_RUNPATH`. So an object with a runpath contributes no rpath to the chain its
dependents search, which is exactly what makes runpath non-inherited, and the
rpath step above draws only from objects that had no runpath of their own.

## Reading the paths WP-31 does not surface

WP-31's validated view carries `DT_NEEDED` and `DT_SONAME` but not `DT_RPATH` or
`DT_RUNPATH`, which it has no other reason to read. Rather than change a
certified parser, this package reads those two tags back out of the dynamic
array WP-31 already proved in bounds. The parser guarantees that the dynamic
section of `dyn_count` entries lies inside the image and that the string table
is bounded by `strsz`; on that footing, reading an `RPATH` or `RUNPATH` value
costs only one more check — that its own string-table offset is below `strsz`
and that the string terminates inside the table — before the value is trusted.
A value that fails those checks is dropped, the same conservative choice the
parser makes for a name it cannot vouch for, and the search source is left empty
rather than read out of bounds. Nothing in WP-31 was modified.

The dynamic-string tokens `$ORIGIN`, `$LIB` and `$PLATFORM` (and their `${...}`
forms) are expanded wherever they appear in an rpath, a runpath, or
`LD_LIBRARY_PATH`. `$ORIGIN` is the directory of the object that carries the
path, so an rpath is expanded once, against its own object's directory, at the
moment the object is read — which is what lets an inherited rpath keep meaning
the ancestor's directory rather than the dependent's. `$LIB` is `lib64` and
`$PLATFORM` is `x86_64` on this target; both are configurable, and an unknown
token is left literal rather than deleted.

## The cache and its tool

The cache is the fourth search source and exists so that the common case — a
name that lives in a standard directory — does not walk the filesystem on every
lookup. `ldso_cache.h` defines the format: a header, an array of entries sorted
by soname, and a string table, with every offset checked against the file's own
bounds when it is opened, because a cache is a file on disk that anything may
have written. The format is this project's own rather than glibc's binary cache;
`doc/decisions/0011-ldso-cache-format.md` records why, and what that leaves for
later. `elf-ldconfig` builds one: it scans directories, reads each shared
object's `DT_SONAME` through WP-31, and writes the cache atomically so a reader
never sees a half-written file. A soname found twice keeps the file from the
directory named last, so precedence among directories is the caller's to set.
Since WP-62 its `-f` file is read in el8's `ld.so.conf` shape -- directories,
`#` comments, and an `include` directive whose glob is resolved against the
including file's own directory when relative, exactly how el8's one-line
`include ld.so.conf.d/*.conf` expects -- with a depth cap so a file that
includes itself terminates instead of recursing.

## The tools

`elf-ldd` is the `ldd` equivalent. It walks the graph and prints the objects in
load order the way `ld.so`'s own trace does: the vDSO placeholder, each
dependency as `name => path`, a missing one as `name => not found`, and the
interpreter last. The addresses are zeros because nothing is mapped; the order
is the point. As `ld.so` itself does, it defers every not-found line to the end,
after the interpreter, so its output matches `ldd`'s for a broken graph as well
as a whole one. `--bare` prints one edge per line for the differential to
compare; `--source` annotates each object with the search source that resolved
it, which is how a precedence question is answered by inspection rather than
belief.

`elf-ldconfig` is the cache builder above. Both follow the project's option,
environment, config, default precedence for every setting.

## What it does not do

It resolves names to files and orders them. It does not map or relocate, does
not read a symbol table, and does not match a symbol version — a name resolves
to a file here whether or not the versions inside will later agree, because that
is WP-34's and WP-36's question and answering it early would couple this package
to theirs. It reads `PT_INTERP` only to print it. A file that resolves but is
the wrong ELF class or machine is stepped over rather than taken, which is what
lets a search continue past a stale 32-bit object, but structural validation
beyond that belongs to WP-31, which every resolved object is parsed through.

## Building and testing

The walker and the cache are four translation units — `elf_graph.c`,
`ldso_cache.c` and WP-31's `elf_parse.c`, with `elf_ldd.c` or `ldconfig.c` on
top — and build under the pinned host toolchain with no host-specific headers.
`t/` holds the certification, and it holds the walk to two bars.

`t/mkgraph.sh` builds the graphs the tests walk, each a set of tiny shared
objects and one non-PIE root emitted by the cross toolchain, so they are real
x86-64 Linux ELF that a real `ld.so` also has an opinion about. They carry no
libc: every `DT_NEEDED` points at another object in the graph, so resolution
stays inside the directories the test controls. The cases are a diamond, an
`DT_RPATH`-versus-`LD_LIBRARY_PATH` precedence pair, a `DT_RUNPATH` inheritance
pair, an `$ORIGIN` lookup, a name reachable only through the cache, and a
missing dependency. Their rpaths and runpaths are written `$ORIGIN`-relative,
which resolves the same whether the tree is read as `/c/...` under the host or
`/mnt/c/...` under a real `ld.so`.

`t/graph_test.c` is the unit bar. It asserts what a differential cannot see:
the breadth-first order and the recorded loader of the shared node, which search
source resolved each name, that rpath reaches a transitive dependency and
runpath does not, that a missing dependency is a flagged node rather than a gap,
that a cache-only name resolves through the cache, and that the cache reader
refuses a corrupt file — a flipped magic byte and a truncated header — rather
than trusting it.

`t/diff-ldso.sh` is the differential bar, and it is the done-when. For each
graph it runs `elf-ldd` and, through WSL, the host's own glibc `ld.so` in its
tracing mode, and compares the ordered list of resolved objects. The two run in
different filesystem namespaces, so both are normalised by stripping the path
down to the graph directory, which lets the namespaces compare equal while still
showing which directory a name resolved in — the point on which the precedence
cases turn, since the same soname sits in two directories and only the winner's
path is printed. A real `ld.so` is required; where none is reachable the script
exits with a skip rather than a failure, and the certification host has one.
`t/run.sh` builds everything, runs both bars, and reports through the session
monitor.

On the certification run all unit checks passed and all seven graphs matched a
real glibc `ld.so` (Ubuntu's 2.43) object for object: the diamond in
breadth-first order with its shared dependency once, rpath winning over
`LD_LIBRARY_PATH` and `LD_LIBRARY_PATH` winning over runpath, rpath reaching a
transitive dependency where runpath left it not found, `$ORIGIN` resolving to
the object's directory, the cache resolving a name no path reached, and the
missing dependency reported not-found in the same position `ldd` reports it.

## Not verified

The differential is against a modern glibc `ld.so` (2.43), not el8's 2.28. The
five search sources and their precedence are stable across that range and the
constructed cases exercise the precedence rather than any version-specific
behaviour, but the comparison object is newer than the target's, and a real el8
`ld.so` has not been the judge. A real vendor binary's full dependency closure,
resolved against a cache built over a real `lib64`, is a heavier integration
than the constructed graphs and is left to the acceptance harness, where the
default paths and cache contents are the vendor's rather than the test's.
