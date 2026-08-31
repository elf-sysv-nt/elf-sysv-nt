# Wiring the bodies, in slices (WP-56)

Work in progress. The forwards become real resolutions into `elfsysv1.dll`
and the shims become translations through WP-55's tables, sliced by
subsystem, with the slice order taken from spike 12's demand ranking.

## Slice machinery

`cut-slices.py map` scans the el8 headers in `veneer/include` with the
cross compiler's `-aux-info`, under `_GNU_SOURCE` so the GNU extensions
and `_LARGEFILE64` names are declared at all, and writes
`symbol-slice.tsv`: every declared function credited to the header that
declares it, and through `slices.tsv` (header -> slice; row order is the
attribution priority) to its slice. The current map covers 3305 symbols
across 101 headers, and `t/real-map.sh` pins what it leaves out: of the
forward map's wired function rows, 275 are unassigned — the underscore
internals no public header declares, `gets`, and the SunRPC `xdr_*`
family, whose headers el8 moved out of glibc into libtirpc.

`cut-slices.py order` joins the census `demand-ranking.tsv` against that
map and writes `slice-order.tsv` plus one per-slice worklist, ranked by
package demand. Symbols the map does not know land in an `unassigned`
slice rather than disappearing. `t/run-tests.sh` exercises both halves
against fixtures, network-free and cross-toolchain-free.

## The translation core

`gen-xlat.py` turns WP-55's `errno-map.tsv` and `signal-map.tsv` into
`xlat-core.gen.c` / `.gen.h`: four functions (`__esn_errno_up/down`,
`__esn_signal_up/down`) over dense value arrays, the one translation
every down-call wrapper shares. Unclaimed values pass through unchanged.
Where Linux aliases two names onto one value that Cygwin keeps apart
(EDEADLK/EDEADLOCK, ENOTSUP/EOPNOTSUPP), the down direction picks the
side-agreeing value when there is one and otherwise an explicitly named
winner in the generator, never a silent first-row-wins. The generated
files are committed; `t/run-tests.sh` regenerates them, requires
byte-identity, and runs compiled spot checks of both directions.

## The crossing

`gen-wire.py` turns the forward map and the slice map into one slice's
wiring: a bind table (`wire-<slice>.gen.c`) with an `esn_wire_ent` row
per wired symbol, a thunk per forward (`wire-<slice>.gen.s`, a
rip-relative tail jump through the row's slot, `.symver`-bound like the
stub it replaces), and the slice's shim worklist. `wire.c` is the one
bind loop: at load the runtime resolves every export name through a
callback and fills the slots; unresolved rows stay null and are counted.
The mechanism and its alternatives are the bound-table decision record.

## The differential

`diff-slice.sh <slice>` is the per-slice bar: each case under
`diff/<slice>/*.c` prints observable behaviour, the reference side runs
it on the pinned el8 image over WSL (the WP-T2 environment), the
candidate side runs it through the wired veneer, and the slice passes
when every case prints the same lines on both sides. The compiler,
runner, and reference are injectable; `t/run-tests.sh` uses that to
prove host-only that identical sides pass and a garbled candidate is
reported as a divergence, so the harness is trusted before any slice
is judged by it. The first cases live under `diff/string/`.

## Status

The census (spike 12) is complete: 4855 packages probed, none in error,
2009 distinct glibc bindings demanded. Its products are committed under
`spike/demand-census/results/` — the per-binding demand ranking, the
summary, and the slice order the ranking cuts. The order puts string
first, then the unassigned internals (`__cxa_finalize`,
`__stack_chk_fail` and kin, the most-demanded bindings of all), then
stdio, posix, stdlib, filesystem, on down 26 slices.

The string slice's wiring is generated and committed —
`wire-string.gen.c` / `.gen.s` / `.shims.tsv`, 47 rows wired, one shim
(`__errno_location`) — and `t/real-map.sh` pins it byte-identical to its
inputs. Five diff cases cover the mem*, str*, tokenizing, errno, and
argz/envz families;
writing them caught the crt ending main through `_exit`, which dropped
buffered stdout on redirection, fixed in the startup files by the
main-returns-through-exit decision. The pinned rocky8 image carries no
compiler, so `diff-slice.sh` grew a reference fallback: compile with the
candidate's own compiler, which targets el8's glibc, and run the binary
on the image, where the real ld.so and libc supply the behaviour under
test. Exercised end to end with both sides on el8: five cases, all
match. The errno and argz cases were waiting on `linux/errno.h`; the
el8 kernel headers are laid into the sysroot now
(`toolchain/sysroot/kernel-headers`, taught to unpack with `rpmx.py`
where the root has no cpio). Judging the candidate side awaits the
runtime that loads the wired veneer.

The stdio slice follows the same path: its wiring is generated and
committed — `wire-stdio.gen.c` / `.gen.s` / `.shims.tsv`, 98 rows, all
thunks, no shims — and `t/real-map.sh` pins it byte-identical too. Five
diff cases cover formatted output, formatted input, stream positioning
over `tmpfile`, memory streams with the line readers, and named files
through open, reopen, rename, remove. Writing them earned a lesson the
harness enforced for free: printing a call's result and its
side-effected operands in one `printf` is unsequenced, and the two
sides' compilers are entitled to disagree — every case now sequences the
call before printing. `t/el8-run.sh` packages the run-on-the-image
candidate runner the string exercise improvised. Exercised end to end
with both sides on el8: five cases, all match; judging the wired veneer
awaits the same runtime the string slice waits on.

The posix slice is the unistd.h family: 108 rows, all thunks, no shims,
generated and committed as `wire-posix.gen.c` / `.gen.s` / `.shims.tsv`
and pinned byte-identical by `t/real-map.sh`. Six diff cases cover
descriptors (pipe, the dup family, lseek with pread and pwrite,
ftruncate), names (link, symlink, readlink, access, truncate, unlink),
the working directory, process identity as invariants rather than raw
values, fork with the exec family joined through wait, and the
not-a-terminal answers (isatty, ttyname, tcgetpgrp on a pipe) with
confstr and pathconf. Exercised end to end with both sides on el8: six
cases, all match; judging the wired veneer awaits the same runtime the
earlier slices wait on.

The stdlib slice is stdlib.h and inttypes.h minus what malloc.h claims:
97 rows, all thunks, no shims, generated and committed as
`wire-stdlib.gen.c` / `.gen.s` / `.shims.tsv` and pinned byte-identical
by `t/real-map.sh`. Six diff cases cover the strto*/ato* conversions
with endptr and ERANGE, the environment (setenv's clobber flag, putenv,
secure_getenv, clearenv), qsort with qsort_r and bsearch plus the
abs/div families, every seeded generator (rand, rand_r, random with
initstate/setstate, the *48 family), temp names with realpath and
canonicalize_file_name plus system, rpmatch, getsubopt and the C-locale
multibyte no-ops, and leaving — on_exit order and quick_exit skipping
it, observed through fork and wait. Exercised end to end with both
sides on el8: six cases, all match; judging the wired veneer awaits the
runtime the earlier slices wait on.

The filesystem slice is the first with a real shim worklist: 103 rows,
67 thunks and 36 shims, generated and committed as
`wire-filesystem.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. The shims are the
stat family and its layout-bearing kin (`__xstat` and twins, statfs,
statvfs, fcntl, glob, readdir, scandir), whose structs cross the bound
table by translation, not by jump. Seven diff cases cover making nodes
(umask, mkdir, mkdirat, chmod, fchmodat, mkfifo, creat, read back
through stat), the directory stream with telldir/seekdir and fdopendir,
scandir under alphasort against versionsort, glob with GLOB_APPEND and
fnmatch's flag set, open flags with fcntl, locks, posix_fallocate and
the statvfs invariants, file times through utime, utimensat and
futimens with UTIME_OMIT, and tree walks — ftw, nftw, fts — with every
walk's findings sorted so traversal order never decides. Writing them
caught the sysroot linking no stat at all: el8 supplies stat, fstat,
lstat, fstatat, mknod as libc_nonshared.a wrappers over the versioned
`__xstat` entries, and the veneer's sliver now does the same. Exercised
end to end with both sides on el8: seven cases, all match; judging the
wired veneer awaits the runtime the earlier slices wait on.

The memory slice is small and all thunks: 21 rows, generated and
committed as `wire-memory.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical by `t/real-map.sh`. malloc and its family forward whole
— the allocator behind the bound table serves both sides of the veneer
— and the mmap family's flag translation belongs to the runtime
downstream of the bind, so the shim worklist is empty for now. Five
diff cases cover the allocator's contract (malloc, calloc's zeroing,
realloc preserving contents across grow and shrink, reallocarray
refusing the overflowing multiplication, free(NULL)), the aligned
allocators with malloc_usable_size as capacity invariants, the mapping
family (anonymous and file-backed mmap with mprotect, msync, munmap,
the zero-length EINVAL, and the mmap64 twin), paging advice with page
locking (madvise, posix_madvise returning the error rather than
setting errno, mlock, munlock), and the allocator's introspection
(mallopt, mallinfo, malloc_trim) printed as invariants over a known
load, never as raw counters. Exercised end to end with both sides on
el8: five cases, all match; judging the wired veneer awaits the same
runtime.

The sockets slice is all thunks too: 66 rows, generated and committed
as `wire-sockets.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical by `t/real-map.sh`. The sockaddr family crosses by
pointer and length, laid out the same on both sides, and the flag
translation the mmap family taught us belongs downstream of the bind
here too, so the shim worklist is empty for now. Seven diff cases
cover byte order with address text (htons round-trips, inet_aton's
classful and hex forms, inet_ntop/pton over v4, v6 and the v4-mapped
compression), socketpair with the message calls (MSG_PEEK, vectored
sendmsg/recvmsg, half-close read as EOF and written as EPIPE), a
loopback TCP conversation (bind to an ephemeral port, accept4 with
SOCK_CLOEXEC observed through fcntl, both ends agreeing on who is who
through the name calls), datagrams (sender identification, message
boundaries held across two sends, a connected UDP socket, EAGAIN
under MSG_DONTWAIT), resolver-free name resolution (getaddrinfo under
AI_NUMERICHOST both families, getnameinfo turning it back, EAI_NONAME
and the error strings), the services and protocols databases against
the well-known entries, and socket options as round-trips and
refusals (SO_REUSEADDR, SO_LINGER, ENOTCONN, EBADF), with every
refusal's call sequenced before its printf per the stdio lesson.
Exercised end to end with both sides on el8: seven cases, all match;
judging the wired veneer awaits the runtime the earlier slices wait
on.

The locale slice is all thunks as well: 83 rows, generated and
committed as `wire-locale.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical by `t/real-map.sh`. The ctype classifications and
their `_l` twins cross by value, `locale_t` is an opaque pointer on
both sides, and whatever category translation setlocale needs belongs
downstream of the bind, so the shim worklist is empty for now.
Pinning it surfaced a residue of the sockets pin: that slice's
`.gen.s` and `.shims.tsv` still named `fwd-sockets.tsv` as their
input, failing the byte-identity check on the first byte compared,
and `.gitignore`'s `*local*` guard was catching the slice by name, so
the wiring subtree now carries the same exception the vendored
headers already had. Seven diff cases cover the narrow classes
counted over 0..255 with the case maps as round-trips and EOF through
every classifier, the wide classes with the named lookups
(wctype/wctrans agreeing with their direct twins), the locale-object
family (newlocale, duplocale, uselocale round-trips against
LC_GLOBAL_LOCALE, `_l` classifiers agreeing through an explicit
object), setlocale with localeconv (the startup default, C and POSIX
round-trips, the refusal of a made-up name, the C lconv field by
field), nl_langinfo across the calendar, formats, radix and yes/no
expressions with the `_l` twin agreeing, strfmon (national and
international forms, width and precision, the E2BIG refusal), and the
message catalogs without a catalog (catopen refusing, catgets handing
back the caller's default, catclose refusing the failed descriptor).
Exercised end to end with both sides on el8: seven cases, all match;
judging the wired veneer awaits the runtime the earlier slices wait
on.

The time slice is all thunks as well: 40 rows, generated and committed
as `wire-time.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical by `t/real-map.sh`. `struct tm` and `struct timespec`
lay out the same on both sides and cross by pointer, and the clockid
and itimer value translations belong downstream of the bind, so the
shim worklist is empty for now. Seven diff cases cover the broken-down
conversions (gmtime, localtime, mktime, timegm and the `_r` twins on
fixed epochs under TZ=UTC, with mktime normalizing an overflowing
field), strftime over the conversion set with strftime_l and the
too-small buffer refusing with 0, strptime with the unconsumed tail,
case-blind month names and the NULL refusals, the posix clocks as
invariants (monotonic ordering, resolution, the cpu clock through
clock_getcpuclockid, timespec_get, both nanosleeps with their EINVAL
refusals), file times set to fixed instants through utimes, futimes,
futimesat and lutimes and read back through stat — lutimes observed on
the link, not the target — itimers reading back at or under what was
armed with the interval held exactly, and tzset over explicit POSIX
zone strings (a fixed offset and a rule-carrying DST form) so no zone
file decides what either side believes. Exercised end to end with both
sides on el8: seven cases, all match; judging the wired veneer awaits
the runtime the earlier slices wait on.

The signal slice is the second with a real shim worklist: 28 rows, 12
thunks and 16 shims, generated and committed as `wire-signal.gen.c` /
`.gen.s` / `.shims.tsv` and pinned byte-identical — counts included —
by `t/real-map.sh`. The shims are the sigset_t bearers — sigaction,
the set-manipulation family, sigprocmask, sigpending, the wait family,
sigqueue, sigsuspend, sigaltstack and both sysv_signal spellings —
whose 128-byte Linux mask crosses the bound table by translation, not
by jump. Eight diff cases cover the set algebra with the out-of-range
refusals, sigaction installing a handler that raise delivers
synchronously with sa_mask observed from inside it and the
SIGKILL refusal, signal's sticky semantics against sysv_signal's
one-shot, sigprocmask holding a raise for sigpending to see and
releasing it, the synchronous wait family with sigqueue's payload
carried through siginfo_t and the zero-timeout EAGAIN, the naming
surface with stderr folded onto stdout so psignal and psiginfo are
observable, sigaltstack installing and observing SS_ONSTACK from an
SA_ONSTACK handler with the bad-flags and undersized refusals, the
System V holding surface down to killpg on the caller's own group,
and sigsuspend over an already-pending signal refusing EINTR with the
caller's mask coming back untouched. Exercised end to end with both
sides on el8: eight cases, all match; judging the wired veneer awaits
the runtime the earlier slices wait on.
