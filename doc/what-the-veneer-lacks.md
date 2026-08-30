# What the veneer does not have

This is the honest inventory of the symbols el8's glibc exports that nothing on
this platform stands behind. It is the fourth of WP-52's four buckets, published
rather than filed, because it is the document anyone deciding whether to depend
on this platform should read first: it says, by name, what a package linked
against the real glibc can call that a package linked against this veneer cannot.

It is generated, not written. `veneer/classification/classify.py` sorts every
one of the version map's symbols (WP-51) against the runtime's actual export
surface (WP-20, `runtime/exports/cygwin-exports.tsv`, the outward face of
`elfsysv1.dll`). A symbol the runtime exports under the same name forwards; one
it exports under a reserved-name or large-file alias forwards under that name; a
handful the runtime almost provides need a translating shim; and the rest — the
subject of this document — have nothing behind them and become stubs that fail
predictably. The complete categorized list is
`veneer/classification/bucket4-inventory.tsv`; this document is its reading.

## Where the fourth bucket sits in the whole map

The version map is 4024 symbol-to-node bindings across `libc.so.6` and its eight
companions. The classification partitions all of them:

    bucket           disposition     rows
    1  forward-alias  forwards under another name    353
    2  forward-same   forwards under the same name  1614
    3  shim           needs a translating wrapper    192   (all flagged for review)
    4  stub           nothing behind it             1797
       scaffold       version-node identity objects   68

The fourth bucket holds 1797 of the map's rows. That is not 1797 useful
functions a package will miss — a large part of it is glibc's own internals,
which no well-formed package links against. The value of this inventory is in
separating the internals a consumer never names from the public interfaces a
consumer might actually reach for and will not find. The categories below draw
that line.

## The categories

    category               rows   what it is
    public-absent           688   public interfaces with no implementation here
    float-n-math            506   _FloatN / _FloatNx IEEE math and string variants
    internal-helpers        285   __-prefixed glibc-internal helpers
    stdio-internals         134   _IO_* libio internals behind FILE
    resolver-internals       54   __res_/ns_ DNS resolver internals
    underscore-internals     38   other _-prefixed internals
    fast-math-no-base        33   __*_finite variants whose base is also absent
    pthread-internals        27   __pthread_/pthread_*_np internals and extensions
    loader-internals         19   ld.so hooks and loader-private symbols
    argp                      9   the GNU argp command-line parser
    fortify-chk-no-base       4   __*_chk variants whose base is also absent

### The internals, which do not matter to a consumer

Seven of these categories are glibc talking to itself. `stdio-internals`
(`_IO_2_1_stdout_`, `_IO_file_xsputn` and the rest of libio) sit behind `FILE`,
and a package uses `FILE` through `fopen` and `fprintf`, which are present, not
through the `_IO_` symbols. `internal-helpers` and `underscore-internals` are
the double-underscore and single-underscore names glibc uses to call its own
code without going through the public alias; a conforming package does not name
them. `resolver-internals` is the private `__res_`/`ns_` machinery under
`res_query` and `getaddrinfo`. `loader-internals` (`_dl_addr`, `_dl_open_hook`,
`__libc_dlopen_mode`) belong to `ld.so`, which on this platform is our own
loader, not glibc's. `fast-math-no-base` and `fortify-chk-no-base` are the
compiler-emitted `__*_finite` and `__*_chk` variants for functions the runtime
does not carry at all; where the base function does exist they are shims
(bucket 3), and only the ones with no base fall here. A package that links only
these internals is malformed; their absence is correct, not a gap.

### float-n-math: the IEEE interchange-type surface

The 506 `float-n-math` symbols are glibc 2.28's TS 18661-3 additions: `acosf128`,
`strtof64x`, `__mulsc3`-adjacent `_FloatN` math for `_Float16`/`_Float32`/
`_Float64`/`_Float128` and their `x` extended forms. newlib has the ordinary
`float`, `double` and `long double` math, which is present; it does not have the
interchange-type family. A package using `_Float128` math will not link. This is
a real absence, but a narrow and modern one, and separated out here so it does
not inflate the count of ordinary functions that are missing.

### public-absent and argp: the absences that will actually bite

The 688 `public-absent` symbols plus the 9 `argp` entries are the part of this
document that matters when deciding whether to depend on the platform. These are
documented, public interfaces, and their absence is what a real package will hit.

The largest coherent group is Linux-kernel-specific interfaces that Cygwin never
had, because they are Linux system calls with no Windows equivalent glibc's
wrappers could sit on: `epoll_create`/`epoll_wait`, `inotify_init`,
`fanotify_init`, `eventfd`, `signalfd`, `timerfd_create`, `memfd_create`,
`statx`, `copy_file_range`, `splice`, `vmsplice`, `process_vm_readv`,
`name_to_handle_at`, `preadv`/`pwritev`, `sendmmsg`/`recvmmsg`, `getauxval`,
`arch_prctl`, `adjtimex`, `bdflush`. A package that reaches for `epoll` or
`inotify` — a great many network and file-watching daemons do — does not build
against this platform unchanged, and that is a property of the host, not a gap
someone forgot to fill.

Beside those are self-contained subsystems glibc ships that newlib does not: the
GNU `argp` command-line parser (`argp_parse`, `argp_usage` and the seven other
`argp_*`), the Sun RPC authentication family (`authdes_create`,
`authunix_create`, `authnone_create` and their kin), `backtrace` and
`backtrace_symbols`, `addmntent`, `addseverity`. Some of these are choices el8's
glibc made that a platform could re-implement later; none of them exists today.

A few entries here are absent only because nothing has been wired to provide
them yet, not because the platform cannot. `getauxval` is the clearest: WP-40
already builds the auxiliary vector this call would read, so it is a stub for now
rather than a permanent lack. Entries like that are stubs in this pass because
the classification is by name against today's export surface; a later work
package can move them into a shim or a forward, and the reproduce test will show
the count fall when it does.

## The caveat this inventory carries

This is a first pass by name and availability. Presence means the name is on the
runtime's export surface; it does not prove the function behind it behaves as
glibc's does — that question is bucket 3, the shims flagged for review, and it is
deliberately not answered here. Absence, by contrast, is a firmer statement: a
name not on the surface is not callable, whatever its semantics would have been.
So this document understates rather than overstates what the platform provides —
some symbols counted as forwards in buckets 1 and 2 will, on inspection, need
shims — and the list of outright absences below is the reliable floor.

## Regenerating

    cd veneer/classification
    ./classify.py -o classification.tsv --bucket4 bucket4-inventory.tsv --summary
    bash t/reproduce.sh

`--summary` prints the bucket and category counts quoted above. `t/reproduce.sh`
reruns the classification, diffs it against the committed files, asserts the
partition covers the map with nothing unclassified, checks that every shim is
flagged for review, and confirms the counts in this document still match what the
generator produces, so this reading cannot drift from the data behind it.
