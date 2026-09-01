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

The process slice wires 47 rows, 43 thunks and 4 shims, generated and
committed as `wire-process.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. The shims are
the rlimit family — getrlimit and setrlimit with their 64 spellings —
whose struct rlimit crosses by translation; the spawn machinery, the
scheduler surface, the wait family and the priority pair cross as
thunks. Eight diff cases cover waitpid seeing an exit status and a
termination signal through the status macros with ECHILD once the
children are drained, wait3 and wait4 filling a child's rusage
alongside getrusage on the caller with the bad-who refusal,
posix_spawn through the shell and posix_spawnp on PATH with the
ENOENT refusal returned rather than delivered through a half-born
child and a file-actions chain redirecting the child's stdout, every
spawnattr getter observing what its setter stored across flags, the
process group, the sched policy and parameter and both signal sets,
the scheduler's priority ranges with the bad-policy refusal and the
caller's own policy, parameter, yield and round-robin interval, the
affinity mask read back non-empty, written back unchanged and the
empty-mask refusal, the NOFILE limit lowered and observed stuck with
cur-over-max and bad-resource refused and the 64 spelling agreeing,
and getpriority's errno protocol with setpriority making the process
nicer, the bad-which refusal and ESRCH for a process that is not
there. Exercised end to end with both sides on el8: eight cases, all
match; judging the wired veneer awaits the runtime the earlier slices
wait on.

The identity slice wires 17 rows, all thunks, no shims, generated and
committed as `wire-identity.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included, the empty shims file among them — by
`t/real-map.sh`. The pwd and grp families cross whole: lookup by name
and by id, the iteration triples, the `_r` variants, and the
group-membership trio. Four diff cases cover the lookups agreeing on
root from both directions with the home and shell rooted and the
not-found NULL for a user and a group that are not there, each `_r`
variant agreeing with its plain sibling plus the ERANGE refusal when a
four-byte buffer cannot hold the strings and not-found as success with
a NULL result, the passwd and group walks passing root exactly once
with a rewind through setpwent finding it again, and getgrouplist's
two-call protocol — the short first call refusing and reporting the
count, the sized second call delivering root's own gid — with
initgroups refusing a user that is not there and setgroups refusing an
impossible count with EINVAL before it looks at privilege. Exercised
end to end with both sides on el8: four cases, all match; judging the
wired veneer awaits the runtime the earlier slices wait on.

The io-mux slice wires 8 rows, all thunks, no shims, generated and
committed as `wire-io-mux.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included, the empty shims file among them — by
`t/real-map.sh`. The readiness families cross whole: select and
pselect, poll and ppoll, signalfd, and the timerfd trio; the epoll,
eventfd and inotify names the slice map also claims are not wired
dispositions in the forward map, so they stay stubs for now. Four diff
cases cover select seeing an empty pipe as silent under a zero timeout
and readable once written with the write end writable and a closed fd
refused as EBADF, poll printing the same pipe facts with a closed fd
reported as POLLNVAL in-band rather than as an error return, signalfd
carrying a blocked SIGUSR1 as a readable record with the right signo
and pid after a nonblocking read of the idle descriptor returns
EAGAIN, and a one-shot timerfd armed at 50ms that gettime reports
armed, read delivers as exactly one expiration, and a zeroed settime
disarms back to nothing. Exercised end to end with both sides on el8:
four cases, all match; judging the wired veneer awaits the runtime the
earlier slices wait on.

The terminal slice wires 30 rows, 29 thunks and 1 shim, generated and
committed as `wire-terminal.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. The termios
surface and the utmp/utmpx record walkers cross as thunks; ioctl is
the one shim, its request codes a translation rather than a jump. The
pty helpers the slice map also claims — openpty, forkpty, login and
kin — live in libutil on el8, so the forward map never carries them
and they are not this slice's rows; the getut*_r variants and the
getutmp pair stay stubs. Four diff cases cover the cf* family over a
zeroed termios (speed setters round-tripping through the getters,
cfsetspeed feeding both directions, cfmakeraw's edits as flag facts,
a made-up speed refused EINVAL), the tc* control surface refusing a
pipe with ENOTTY call by call and EBADF once the descriptor is gone,
the utmp and utmpx records over private files named through utmpname
and utmpxname — written, found again by id and by line after a
rewind, missed when nothing matches, and updwtmp growing a wtmp file
by exactly one record — and ioctl observed over a pipe: FIONREAD
counting what sits unread, FIONBIO's edit showing up in the fcntl
flags, FIOCLEX setting close-on-exec, a terminal request answering
ENOTTY and a closed descriptor EBADF. Exercised end to end with both
sides on el8: four cases, all match; judging the wired veneer awaits
the runtime the earlier slices wait on.

The misc slice wires 33 rows, all thunks, no shims, generated and
committed as `wire-misc.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included, the empty shims file among them — by
`t/real-map.sh`. The grab-bag headers cross whole: the getopt family,
the err/warn and error reporters, dirname, the search trees and
tables, wordexp, and the random-byte pair. basename is the string
slice's row by attribution (string.h declares it and outranks
libgen.h) and `__xpg_basename` is a stub, so neither is counted here.
Five diff cases cover getopt over fixed argv arrays (arguments
attached and separate, the unknown-option and missing-argument
protocols with and without the leading colon, glibc's permutation
observed through optind and the reordered operands, getopt_long's
value and flag-setting rows with getopt_long_only's single-dash
spelling), the reporters with both program-name variables pinned —
err reads the `__progname` pair where error reads
program_invocation_name, a divergence the case caught — and stderr
folded onto stdout, the exiting forms observed as statuses through
fork and wait, and error_one_per_line suppressing the repeat,
tsearch grown in a fixed order so twalk agrees with tdelete, tdestroy
counting its frees, lfind refusing what lsearch appends, and
insque/remque edits walked, the hsearch tables global and reentrant
(ENTER keeping the first row on a duplicate key, the ESRCH miss, two
`_r` tables independent), and wordexp under a pinned environment (a
two-word variable split, WRDE_APPEND, command substitution refused by
flag, a bad character by value) with dirname over the POSIX examples
and getentropy/getrandom as return-code invariants including the
oversized-buffer EIO. Exercised end to end with both sides on el8:
five cases, all match; judging the wired veneer awaits the runtime
the earlier slices wait on.

The runtime slice is the first where the shims outnumber nothing but
themselves: 10 rows, 5 thunks and 5 shims, generated and committed as
`wire-runtime.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. The context
family and `__assert` cross as thunks; the setjmp/longjmp family are
the shims, their jmp_buf a translation rather than a jump.
`__assert_fail`, the backtrace trio and getauxval are stubs in the
forward map, so they are not this slice's rows. Four diff cases cover
the jump value protocol (0 on the direct return, the sent value on
the jumped one, a longjmp of 0 delivered as 1, a volatile hop
counter proving the jumps happen, and a counted setjmp loop), what
each jump saves of the signal mask — setjmp and _setjmp leave it
alone, sigsetjmp saves it only when asked, observed by blocking
SIGUSR1 in the jumped-from region — the context family (a
swapcontext ping-pong with the trace pinning the order, makecontext
arguments arriving intact, uc_link followed off the end of a
function, and a setcontext loop rerun a counted three times), and
the assert surface (a passing assert silent, a failing one SIGABRT
through fork and wait with its path-bearing message left off the
comparison, `__assert` — whose arguments are ours to pin — compared
message and all, and NDEBUG compiling the failure away). Exercised
end to end with both sides on el8: four cases, all match; judging
the wired veneer awaits the runtime the earlier slices wait on.


The threads slice wires 42 rows, 41 thunks and 1 shim, generated and
committed as `wire-threads.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. These are the
libc-resident pthread subset — the attribute object, the mutex and
condition protocols with the condition rows doubled across their two
version nodes, identity, the scheduling pair, the cancellation
switches — and the C11 quartet (thrd_current, thrd_equal, thrd_sleep,
thrd_yield); the rest of the pthread surface lives in libpthread on
el8, so the forward map never carries it. `__sigsetjmp` is the one
shim, attributed here because el8's pthread.h declares it for the
cleanup macros and outranks setjmp.h; its jmp_buf translation is the
runtime slice's kin and its behaviour is already under that slice's
sigmask case. Four diff cases cover the attribute object (documented
defaults through every getter, each setter read back, the bad detach
state EINVAL and the process scope ENOTSUP), identity with the mutex
protocol and the cancellation switches (pthread_self through
pthread_equal and a copy, a fresh and a static mutex through
lock/unlock/destroy, getschedparam answering SCHED_OTHER at priority
0 and accepting itself back, each switch handing back the state it
replaces and refusing a made-up value), the condition variable's
waiter-free protocol (signal and broadcast with nobody to find, a
timedwait past its deadline handing the mutex back with ETIMEDOUT
and the mutex relocking after), and the C11 quartet (thrd_equal over
the current thread and a copy, a short full sleep answering 0 with
the remainder untouched and time observably advanced, thrd_yield
returning at all). Exercised end to end with both sides on el8: four
cases, all match; judging the wired veneer awaits the runtime the
earlier slices wait on.


The wchar slice wires 87 rows, all thunks and no shims, generated and
committed as `wire-wchar.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical — counts included — by `t/real-map.sh`. The
wide-character surface is value-preserving end to end: wchar_t is 4
bytes on both sides, the conversion states are opaque, and nothing
declared by wchar.h or uchar.h carries an errno-bearing structure of
its own, so every row crosses as a tail jump. The rows are the
conversion state machine (btowc/wctob, the mbr*/wcr* restartable
converters, the string converters and their n-limited kin, and the
C11 mbrtoc16/c16rtomb and mbrtoc32/c32rtomb), the wide string
operators the string slice does not already claim (the copy and
concatenate family, the span and search family, wcstok, wcsdup, the
wmem movers), the wide-to-number converters with their _l variants,
collation and width, and the wide stream protocol down to
open_wmemstream. Four diff cases cover conversion in the C locale
(ASCII round trips through every converter pair, mbsinit on a clean
state, EILSEQ on a 0x80 byte, and the C11 pair rounding the same
trip), the wide string operators (wcpcpy's end pointer, bounded
copies and concatenations, the search family over one haystack,
wcstok walking three fields, an overlapping wmemmove), the number
converters (end-pointer contracts, ERANGE at the rim, wcstod's
fractions, wcsftime over a fixed moment, widths and C-locale
collation), and the wide stream protocol (a swprintf/swscanf round
trip, open_wmemstream collecting three writers, and a tmpfile read
back through fgetws, fgetwc and a pushed-back ungetwc, with fwide
reporting the orientation). Exercised end to end with both sides on
el8: four cases, all match; judging the wired veneer awaits the
runtime the earlier slices wait on.

A later measurement supersedes the value-preserving claim just above.
"wchar_t is 4 bytes on both sides" was read face-against-glibc, both on
el8; the wchar slice's live crossing below -- the first time these rows meet
the real elfsysv1.dll -- finds the body's wchar_t two bytes wide, so the wide
rows do not cross as forwards after all. See "The wchar slice: live
crossing" at the end and the wchar-width decision it records.

The regex slice is the smallest yet: 5 rows, all thunks, no shims,
generated and committed as `wire-regex.gen.c` / `.gen.s` /
`.shims.tsv` and pinned byte-identical — counts included — by
`t/real-map.sh`. The rows are the POSIX four — regcomp, regexec,
regerror, regfree — with regexec carrying both its version nodes;
regex_t and regmatch_t are built and consumed by the same libc on
both sides of the bound table, so everything crosses as a tail jump.
The GNU re_* family are stubs in the forward map and so are not this
slice's rows. Four diff cases cover the compile-and-match core (a
literal's span, subexpression offsets, BRE against ERE over the same
pattern text, REG_NOSUB with re_nsub still counted, back-references
binding to the captured text), bracket expressions and case (ranges,
negation, the named classes in the C locale, REG_ICASE folding both
forms, intervals in both syntaxes), the refusals (each bad pattern
named by its code, regerror's text for every code it mints, and the
truncation contract — the returned length counts the whole message
while a short buffer gets a terminated prefix), and the anchors
(REG_NOTBOL and REG_NOTEOL turning them off, REG_NEWLINE making them
line-relative and stopping dot at the break, the empty pattern, and
a match-by-match scan through rm_eo). Exercised end to end with both
sides on el8: four cases, all match; judging the wired veneer awaits
the runtime the earlier slices wait on.

The syslog slice matches regex for size: 5 rows, all thunks, no
shims, generated and committed as `wire-syslog.gen.c` / `.gen.s` /
`.shims.tsv` and pinned byte-identical — counts included — by
`t/real-map.sh`. The rows are the syslog.h five — openlog, syslog,
closelog, setlogmask, vsyslog — every one forward-same at
GLIBC_2.2.5; the format string and the mask value mean the same
thing on both sides of the bound table, so everything crosses as a
tail jump. Four diff cases lean on LOG_PERROR with stderr routed
onto stdout to make the message copy observable: the setlogmask
contract (the initial all-priorities mask, LOG_MASK and LOG_UPTO's
bit patterns, every call returning the mask it replaced, zero as a
read-back that changes nothing), the copied text (the ident prefix,
printf conversions, %m expanding the errno at the call, and exactly
one newline-terminated line whether or not the format supplied one),
the mask gating emission (a dropped priority produces nothing at
all, facility bits play no part in the masking, widening readmits),
and the connection's identity (openlog re-tagging across closelog,
LOG_PERROR riding through it, and vsyslog through a variadic wrapper
printing exactly what the direct call prints — while closelog's
reset of the tag to the program name, which differs between the two
sides' binaries, is deliberately left unprinted). Exercised end to
end with both sides on el8: four cases, all match; judging the wired
veneer awaits the runtime the earlier slices wait on.

The sysv-ipc slice is thirteen rows, all thunks, no shims, generated
and committed as `wire-sysv-ipc.gen.c` / `.gen.s` / `.shims.tsv` and
pinned byte-identical — counts included — by `t/real-map.sh`. The
rows are ftok, the msg/sem/shm call families off sys/ipc.h and
friends, and `__getpagesize`, which sys/shm.h declares for SHMLBA;
every one forward-same at GLIBC_2.2.5, since keys, identifiers, and
operation structs mean the same thing on both sides of the bound
table. Four diff cases cover the ftok contract (a stable key from
the same path and low byte, different low bytes naming different
keys, and ENOENT on a missing path), semaphores (SETALL and GETALL
round-tripping a set, semop applying an array atomically, IPC_NOWAIT
turning a would-block into EAGAIN without touching any value, and
GETPID/GETNCNT/GETZCNT), shared memory end to end (a second
attachment seeing a first attachment's write, IPC_STAT's size and
attach count, detach dropping the count, and removal outliving the
existing mapping while blocking new attachments with EINVAL), and
message queues (a zero-length body round-tripping, MSG_NOERROR
truncating an oversized receive instead of failing, a
type-selective receive picking the right message out of several
queued, IPC_NOWAIT turning an empty queue into ENOMSG, and removal
failing a further send with EIDRM). Exercised end to end with both
sides on el8: four cases, all match; judging the wired veneer awaits
the runtime the earlier slices wait on.

The io slice is the smallest yet: 2 rows, both thunks, no shims,
generated and committed as `wire-io.gen.c` / `.gen.s` / `.shims.tsv`
and pinned byte-identical — counts included, the empty shims file
among them — by `t/real-map.sh`. readv and writev cross whole,
forward-same at GLIBC_2.2.5; the rest of the slice's map rows stay
stub. The aio_* family and lio_listio are not in libc's version map
at all — el8 carries asynchronous I/O in librt, WP-54 territory, the
same split the threads slice found for the rest of pthread — sendfile
and its 64 twin and process_vm_readv/writev have no Cygwin-side
equivalent to forward to, and the preadv/pwritev family and their v2
kin are classification stubs alongside them, so none of the seven are
this slice's rows. One diff case covers the two wired calls: writev
gathering three buffers, one of them zero-length, into a single pipe
write, and readv scattering the bytes back across three buffers whose
sizes do not line up with the writer's. Exercised end to end with
both sides on el8: one case, match; judging the wired veneer awaits
the runtime the earlier slices wait on.

The system slice is six rows, all thunks, none shims, generated and
committed as `wire-system.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical -- counts included, the empty shims file among them --
by `t/real-map.sh`. The rows are uname and the get_nprocs / get_*_pages
family off sys/utsname.h and sys/sysinfo.h, every one forward-same at
GLIBC_2.2.5; a struct copy or a scalar count means the same thing on
both sides of the bound table, so everything crosses as a tail jump.
One diff case covers the six calls through invariants rather than raw
values, since a hostname, kernel release, memory size and CPU count
differ between the reference machine and wherever the case runs: the
uname fields all read non-empty, sysinfo succeeds with uptime
non-negative and free memory no greater than total, and the two ways
of asking after CPUs and pages -- get_nprocs against get_nprocs_conf,
get_avphys_pages against get_phys_pages -- keep their bounding
relationship. Exercised end to end with both sides on el8: one case,
match; judging the wired veneer awaits the runtime the earlier slices
wait on.

The math slice is thirty-four rows, all thunks, none shims, generated
and committed as `wire-math.gen.c` / `.gen.s` / `.shims.tsv` and pinned
byte-identical -- counts included, the empty shims file among them --
by `t/real-map.sh`. The rows are the classification family (isnan,
isinf, finite, and the deprecated leading-underscore spellings el8
still exports) with their f/l twins, plus copysign, frexp, ldexp, modf
and scalbn with theirs; every one forward-same or forward-alias at
GLIBC_2.2.5, since a classification returns a boolean and the rest
split or scale a value in place, so nothing here carries a struct
across the bound table. complex.h and fenv.h declare nothing this
forward map wires, so the slice's rows are math.h's alone. One diff
case covers all thirty-four through values with an exact binary
representation -- an ordinary value, infinity, and nan on each width
for the classifications, and 12.0/0.75 and 3.5 for the split and scale
functions -- so both sides print the same bits, not just the same
rounded decimal. Exercised end to end with both sides on el8: one
case, match; judging the wired veneer awaits the runtime the earlier
slices wait on.

The dl slice is not yet wireable: `link.h`'s and `dlfcn.h`'s rows are
all stub in the forward map today (`_dl_mcount_wrapper_check` and
`dl_iterate_phdr`, disposition stub; `dlopen`, `dlclose`, `dlsym`,
`dlerror` and the rest are not in the map at all), since dynamic
loading is the runtime's job, not a libc forward -- `gen-wire.py`
confirms this with "nothing to wire" for the slice today. It is left
for the runtime work the earlier slices are already waiting on, not
for another pass of this generator.

Of the census's 26 slices, 23 are wired: string, stdio, posix,
stdlib, filesystem, memory, sockets, locale, time, signal, process,
identity, io-mux, terminal, misc, runtime, threads, wchar, regex,
syslog, sysv-ipc, io, system and math. Two are open rather than
wired: unassigned, whose 275 rows real-map.sh counts and pins as a
residue check but `gen-wire.py` has never been asked to generate
(the underscore internals no public header declares, `gets`, and the
SunRPC `xdr_*` family), and dl, stub in the forward map today as
above. WP-56's per-slice bar is met for every wireable slice; what
remains is unassigned and dl's own resolution, and the runtime that
judges every wired slice against a real vendor package, per the work
package's overall done-when.

## Live crossing

Every slice above was, until now, judged only on el8: both sides compiled
and run on the pinned Linux image, never against a real `elfsysv1.dll`.
WP-27 (the System V face) landed a faced DLL at
`a/build/wp27-face/elfsysv1.dll` and a working pattern for calling into it
for real -- `runtime/face/t/elfcall.c` resolves NOSIGFE exports out of the
image's own PE export directory and calls them System V, straight at the
export, driven through WP-41's front end and native stub. `t/live-math.c`
and `t/live-math.sh` adapt that pattern to `wire.c`'s own bind loop: a
resolver of `esn_wire_resolver`'s shape stands in for the runtime's
eventual `GetProcAddress` callback, `__esn_wire_bind` runs it over the
math slice's real, committed table (`wire-math.gen.c`, all 34 rows
NOSIGFE, so no full process bring-up is needed), and three of the slice's
generated thunks (`wire-math.gen.s`'s w00010/copysign, w00022/isnan,
w00025/ldexp) are called directly rather than the raw exports, so the code
under test is the wired veneer body itself. Run through WP-41's branch
against the real DLL: the bind loop resolves all 34 rows with none
missing, and all three thunks return the real body's answer -- status 15,
the only pass, with the same refuse-before-entry and no-runtime controls
`elfcall.sh` uses. This is the first slice judged against a real DLL
rather than only on el8, and the first execution of a generated wire
thunk as real candidate code on NT.

It does not extend to a SIGFE-fenced slice (io, system, sysv-ipc, regex,
syslog, and most of the rest): those need the fuller process bring-up
`runtime/face/t/fault.c` uses (the vendor crt0, `cygwin_internal` init,
the thread carrier), not just a resolved export table, and are left for a
later increment. It also does not run any of `diff-slice.sh`'s existing
differential cases through this path -- that wants a hosted C library on
the candidate side (argv/envp, a heap, I/O) that a freestanding specimen
like this one does not have; today's result is the bind loop and the
thunks proven against the real DLL, not a diff case's full behavior.
WP-56's overall done-when (a vendor package compiles, links, runs its own
test suite, and passes) still needs that hosted candidate environment and
the SIGFE-fenced slices' process bring-up, neither of which this increment
attempts.

Every slice above is judged only on el8: both sides compiled and run on the
pinned Linux image, never against a real `elfsysv1.dll`. WP-27 (the System V
face) has since landed a faced DLL at `a/build/wp27-face/elfsysv1.dll` and a
working pattern for calling into it for real -- `runtime/face/t/elfcall.c`
resolves NOSIGFE exports out of the image's own PE export directory and calls
them System V, straight at the export, driven through WP-41's front end and
native stub. The next increment adapts that pattern to `wire.c`'s own bind
loop: a resolver built on the same PE-export walk stands in for the runtime's
eventual `GetProcAddress` callback, `__esn_wire_bind` runs it over the math
slice's real table (all 34 rows NOSIGFE, so no full process bring-up is
needed), and a couple of the slice's generated thunks are called directly to
prove a wired veneer body executes for real on NT -- not the fallback of
comparing the candidate against itself, and not only against el8. This
certifies the bind mechanism against a real DLL and runs real candidate code,
without yet attempting a SIGFE-fenced slice (io, system, sysv-ipc, ...),
which needs the fuller process bring-up `runtime/face/t/fault.c` uses and is
left for a later increment.

The runtime slice is the second live crossing, and the only other slice
(after math) whose whole wired table is NOSIGFE (10 rows). `t/live-runtime.c`
and `t/live-runtime.sh` adapt live-math's shape unchanged: the same
freestanding entry, the same PE-export resolver standing in for
`GetProcAddress`, `__esn_wire_bind` run over the real, committed
`wire-runtime.gen.c` table, and generated thunks called directly rather than
the raw exports. Of the table's 10 rows, five are jmp_buf-translating shims
out of scope for a specimen that only calls thunks. Of the remaining five
thunks, three turned out to need skipping: `__assert` aborts unconditionally
by contract and a freestanding specimen has no safe way to observe an abort;
`makecontext` needs a second stack, more machinery than this increment
attempts; and `setcontext` was skipped for a reason only writing the specimen
surfaced -- its whole observable contract on success is never returning, and
testing `swapcontext`, which performs the same kind of switch, found that the
real DLL's body does not actually transfer control in this freestanding
harness: it returns 0 having only done the save half of the operation, not
the restore half. That leaves `getcontext` (w00003) and `swapcontext`
(w00009), and both checks were redesigned around that finding to observe a
save-side effect -- a sentinel-filled `ucontext_t` buffer gets overwritten --
rather than assume a completed control transfer, with the swapcontext check
guarded against ever running twice in case a future runtime does perform the
switch and resumes there. Run through WP-41's branch against the real DLL:
the bind loop resolves all 10 rows with none missing, and both thunks reach
the real body and touch their buffers -- status 7, the only pass, with the
same refuse-before-entry and no-runtime controls live-math.sh uses.

This confirms live crossing generalizes past math (a second slice's bind
table and thunks run for real against the real DLL), and it narrows what
"NOSIGFE" actually promises: the classification is about the calling
convention a thunk needs, not about whether the body behind it is complete.
`getcontext` behaves as documented; `setcontext`/`swapcontext`'s actual
context switch does not happen under this specimen's minimal process state,
which this increment surfaces as a finding rather than papering over with a
weaker but still-honest pair of checks. Whether that gap closes with fuller
Cygwin per-thread bring-up (cygtls and friends) or needs a fix in the face
layer is left for whoever next touches the runtime slice's shims -- the five
jmp_buf-translating rows this specimen did not attempt are the next thing to
try, and setjmp/longjmp's simpler save/restore contract (no signal mask, no
stack switch) may turn out to be easier to prove live than context switching
was.

## The jmp_buf shims: layout investigation

Before touching the five jmp_buf-translating rows the runtime slice left
open (`_setjmp`, `setjmp`, `_longjmp`, `longjmp`, `siglongjmp`), this
increment checked what each side's buffer actually looks like, since the
earlier note's guess -- that setjmp's "simpler save/restore contract" might
be easier to prove live than context switching -- turns out to understate
the problem.

El8's `jmp_buf` is `bits/setjmp.h`'s `__jmp_buf`: 8 `long`s, 64 bytes on
x86_64, holding the callee-saved registers (rbx, rbp, r12-r15, rsp, rip)
with rsp and rip mangled against a per-thread guard glibc keeps at
`%fs:0x30` (`PTR_MANGLE`/`PTR_DEMANGLE`) -- a security hardening this
project's translation tables have not needed to account for anywhere
else, since nothing else crossing the bound table carries a
pointer-obfuscated field. `sigjmp_buf` is the same `__jmp_buf_tag`
layout plus a saved-mask flag and a 128-byte `sigset_t`, matching the
signal slice's mask width.

Cygwin's `jmp_buf`, read from this root's own
`/usr/include/machine/setjmp.h` (the newlib header the project's own
runtime build compiles against, so this is the real target-side layout,
not a guess): on `__x86_64__` with `__CYGWIN__` defined, `_JBTYPE` is
`long` and `_JBLEN` is 32 -- 256 bytes, four times el8's. Cygwin has
carried real `sigsetjmp`/`siglongjmp` functions since 2.2.0 rather than
glibc's macro pair, and its `sigjmp_buf` appends a save-mask slot and a
`sigset_t`-sized run of words after the 32-long body via the same
`_SAVEMASK`/`_SIGMASK` scheme el8 uses, so the two sides agree on the
*shape* of that convention while disagreeing by 4x on the body it is
appended to.

That size mismatch is the actual blocker, not the register set. A
forward's caller on the el8 side allocates 64 bytes of stack (or struct
space) for its `jmp_buf` -- callers never inspect a `jmp_buf`'s fields,
POSIX only requires it round-trip through the same implementation's own
`setjmp`/`longjmp`, so its interior is opaque to every conforming
caller. A shim that simply tail-called Cygwin's real `setjmp` against
that same 64-byte region would let Cygwin's body write up to 256 bytes
into a 64-byte allocation -- corrupting whatever the caller placed next
on its stack, silently, on the first call. This is a different kind of
divergence than any shim wired so far: every existing shim (the stat
family's structs, sigaction's mask, rlimit, ioctl's request codes)
translates a *representation* of the same conceptual size class field
by field; here the two sides' opaque blobs are not just laid out
differently, one is categorically larger than the other, so there is no
in-place translation to write.

The shim therefore needs an out-of-line real buffer: allocate a real
256-byte Cygwin `jmp_buf` somewhere the el8-shaped 64-byte buffer can
lead back to (a pointer stashed in the caller's buffer, or a table keyed
by the buffer's address), call Cygwin's `setjmp` against the real
buffer, and record enough state that the matching `longjmp` can find the
same real buffer and call Cygwin's real `longjmp` on it. That surfaces
its own open questions this increment does not resolve: where the real
buffer lives (thread-local slot vs. a table, and its capacity if a
caller nests several live jumps), what happens when a `jmp_buf` is
copied by value (POSIX allows storing one in a struct and copying the
struct; a pointer-based side table would then point two names at one
real buffer, silently sharing state the caller believes are
independent copies), and whether the pointer-guard mangling on el8's
side needs to be reproduced at all given that no code but this shim
ever reads the mangled bits directly. None of this is close to the
mechanical, thunk-only translations the earlier 23 slices needed, so it
is left as scoped-out groundwork rather than an attempted implementation
this run -- the finding is that the shim is a buffer-identity problem,
not a field-translation one, before any register layout work starts.

## The jmp_buf shims: a frameless face, not a call-style wrapper

The buffer-identity investigation above framed the open problem as sizing
alone. It is not the whole problem: DR-0041, recorded one seam down at the
sv2ms face, already found that setjmp/longjmp must never be wrapped in an
out-of-line call-style function, because the pair captures the literal
calling frame, not a value handed to it. A call-style wiring shim for this
family would reproduce that dead-frame bug regardless of whether the buffer
sizes were reconciled. DR-0051 (the jmp_buf shims take a frameless face)
connects the two findings and specifies the shape: a hand-written frameless
thunk, the wiring layer's counterpart to `sv2ms-ctx.inc`, that stashes the
real 256-byte Cygwin buffer's address in the caller's own 64-byte el8-shaped
`jmp_buf` (lazily allocated on first use) and `jmp`s into Cygwin's real
`setjmp`/`longjmp` rather than calling them.

The five rows are built now: `wire-jmpbuf-face.inc` holds the two macros
(`wire_jmpbuf_save`, `wire_jmpbuf_restore`) as hand-written assembly, one
per direction, and `gen-jmpbuf-face.sh` emits `wire-jmpbuf-faces.gen.S` from
a curated table, `jmpbuf.tsv`, cross-referenced against the slice's own
generated `wire-runtime.shims.tsv` for each row's version, binding and
bind-table offset. DR-0051 left open whether the generator should grow a
third generated category or take a curated exception list in `ctx.tsv`'s
pattern; the curated-table route was chosen, matching `ctx.tsv` and
`gen-ctx-faces.sh` at the sv2ms seam exactly rather than teaching
`gen-wire.py` itself a call-vs-jump distinction it has never needed before.
`gen-wire.py` did not need to change: it already leaves every shim row out
of a slice's `.gen.s` thunks (a shim's body is hand-written, not a
generated tail jump), so the jmp_buf rows were already absent from
`wire-runtime.gen.s` and the frameless face is the only body that binds
those five `.symver` names. `t/real-map.sh` re-derives
`wire-jmpbuf-faces.gen.S` from `jmpbuf.tsv` and the freshly re-derived
`wire-runtime.shims.tsv` and pins it byte-identical, so a drift in the real
forward map's five jmp_buf rows -- a table-index shift, a binding change --
is caught the same way the ordinary thunks are; where the cross compiler is
available it also assembles the face and checks the five `.symver` names,
the `malloc` and `__esn_wire_runtime` externs, and the lazy-allocation
branch by name. `diff/runtime/jmpbuf-lifecycle.c` exercises the two shapes
this face adds beyond el8's own contract -- a fresh, explicitly zeroed
`jmp_buf`'s first use, and one `jmp_buf` reused across two hundred
setjmp/longjmp round trips -- and needs no separate registration:
`diff-slice.sh`'s glob over `diff/runtime/*.c` picks it up alongside
`jmpfam.c`, `sigmask.c`, `ucontext.c` and `assertok.c`. Running any of
these five against the wired veneer still awaits the runtime the rest of
this document keeps deferring to; what changed in this increment is that
the face itself, its routing, and its differential case are now written,
generated, pinned and reviewed rather than merely specified.

## The jmp_buf shims: live crossing

The five jmp_buf-translating rows the runtime slice's live crossing left
open are the next thing tried, per that section's own closing line, and
setjmp/longjmp's plainer contract does turn out to prove live where
`setcontext`/`swapcontext` did not: `t/live-jmpbuf.c` and
`t/live-jmpbuf.sh` adapt live-runtime's shape -- the same freestanding
entry, the same PE-export resolver, `__esn_wire_bind` run over the real,
committed `wire-runtime.gen.c` table -- and add `wire-jmpbuf-faces.gen.S`
to the link so the frameless face's own generated labels
(`__jmpbuf_setjmp`, `__jmpbuf_longjmp`) can be called directly. Unlike
every earlier live specimen, this one supplies its own `malloc`: a plain
bump allocator over a static arena, since the frameless save macro's
lazy-allocation branch calls through `malloc@GOTPCREL` and this
freestanding, `-nostdlib` specimen has no other libc to provide it.

The check asks a harder question than the runtime slice's did: not
merely whether the real body is reached, but whether calling through the
face actually resumes execution at the setjmp call site carrying the
longjmp'd value -- a full round trip, not a reached-the-body probe.  Run
through WP-41's branch against the real DLL: the bind loop resolves all
10 runtime rows, the setjmp face's first return is 0 with a real Cygwin
buffer pointer stashed in the caller's el8-shaped buffer, the longjmp
face's call is never seen to return normally, and the setjmp call site's
second return carries the value 42 with the stashed pointer unchanged --
status 15, the only pass, with the same refuse-before-entry and
no-runtime controls the earlier live specimens use.

Getting a reproducible pass took two rounds of the same lesson C's own
setjmp contract states and this specimen's first drafts didn't fully
honor: every value read after the first return and depended on after the
second must be `volatile`, and that includes values the specimen itself
computes and mutates around the jump, not only the jmp_buf's own
contents. `__jmpbuf_setjmp` needed `__attribute__((returns_twice))` on
its extern declaration before GCC would even keep the caller's frame
state honest across the call. Once that was in place, the status
accumulator (`status`, built up with `|=` both before the setjmp call and
after the resumed return) still came back missing bits set in the direct
branch: a plain automatic variable modified after `setjmp` and read after
`longjmp` is indeterminate by the C standard, and GCC's own
`-Wclobbered` said as much before this was tracked down. Marking `status`
`volatile` fixed it. The stashed-pointer reads were changed from a
`(void * volatile *)`-cast dereference of the el8 buffer to a plain
byte-by-byte accumulation for the same reason type-punning a volatile
object invites in the first place -- not because the cast read was ever
shown wrong, but because the byte-wise form leaves nothing for alignment
or strict-aliasing to arguably license optimizing around. None of this
touches the frameless face itself, which needed no changes once the
specimen calling it was correct; the finding is entirely in what a
C caller owes a function the compiler must be told can return twice, one
seam up from the face's own hand-written assembly.

This is the first slice whose shim rows, not just its thunk rows, are
proven live end to end -- the buffer-identity mechanism DR-0051 designed
and the previous increment only pinned by construction now has a real
Cygwin `setjmp`/`longjmp` pair executing through it on NT. The other
three jmp_buf rows (`_setjmp`, `_longjmp`, `siglongjmp`) share the same
two macro bodies with only a symver alias differing, so this is evidence
for the mechanism those rows depend on too, not an independent proof of
each. WP-56's overall done-when -- a vendor package's own test suite,
run and passed -- still needs the SIGFE-fenced slices' fuller process
bring-up and a hosted candidate environment, neither of which this
increment attempts.

## The remaining jmp_buf alias rows: live crossing

The predecessor section's own closing line named the gap directly: proving
setjmp/longjmp live is evidence for the mechanism the other three jmp_buf
rows (`_setjmp`, `_longjmp`, `siglongjmp`) share, not an independent proof
of each. This increment closes that gap the same way the runtime slice's
was closed -- by asking the harder question directly rather than resting
on the inference. `t/live-jmpbuf.c` now runs three round trips instead of
one, each through its own 64-byte el8-shaped buffer so a later round's
save can never overwrite an earlier round's already-allocated real Cygwin
buffer: setjmp/longjmp (unchanged from the predecessor), _setjmp/_longjmp
(`__jmpbuf__setjmp` / `__jmpbuf__longjmp`), and setjmp/siglongjmp
(`__jmpbuf_setjmp` saving, `__jmpbuf_siglongjmp` restoring). The third
pairing needs a word: there is no sigsetjmp row in the five-row jmp_buf
census, so siglongjmp has no save counterpart of its own to pair with in
this specimen; restoring a buffer that plain setjmp saved is a legitimate
probe of `wire_jmpbuf_restore`'s macro instantiation for the siglongjmp
symver alias even though no real caller mixes the two spellings this way.

All three rounds share one body now, `JMPBUF_ROUND`, a macro rather than a
function because the save half of each round calls a function the compiler
must be told `returns_twice`, and that attribute's obligations fall on the
frame containing the direct call -- the same lesson the predecessor
increment's README account already worked out for the single setjmp call
it had. Folding three copies of that call into a macro expanded inline at
each of the three call sites keeps that obligation where it belongs
without hand-duplicating the round-trip logic three times.

Run through WP-41's branch against the real DLL: all three rounds pass --
each one's forward leg stashes a nonzero real-buffer pointer that survives
unchanged to the restore leg, and each restore leg's return value carries
the sent value (42), the same live proof-of-transfer the predecessor
increment established for the first pair alone. The status word's bit
layout changed to fit three rounds' worth of checks in one byte: the
stash-unchanged check that used to be its own bit (0x08) is now folded
into the same bit as the forward-leg check per round, freeing 0x10/0x20
and 0x40/0x80 for the second and third rounds. The only passing status is
now 0xF7 (247), not 0x0F (15); 0x08 itself is retired and nothing sets it.
`t/live-jmpbuf.sh` was updated to match.

This is the first evidence, not just inference, that all five jmp_buf rows
-- the full set DR-0051's frameless face was built to cover -- round-trip
live against a real Cygwin body on NT. It does not touch the SIGFE-fenced
slices or the `unassigned`/`dl` rows, and WP-56's overall done-when -- a
vendor package's own test suite, run and passed -- is unchanged by this
increment: still pending the fuller process bring-up and a hosted
candidate environment neither this nor the increment before it attempts.

The dl slice closes the slice order without wiring a row at the libc
face. dlopen, dlsym, dlclose, dlerror, dlvsym, dlinfo, dlmopen and the
dladdr pair -- everything dlfcn.h declares -- live in libdl.so.2 on el8,
and link.h's dl_iterate_phdr and the la_* auditing hooks belong to the
dynamic loader rather than libc; so the libc forward map carries none of
them, exactly as it carries none of io's aio family or terminal's pty
helpers, all of them WP-54's DSOs rather than this one's. gen-wire
reports the slice empty and writes no files. The two names the slice
map does place in libc -- dl_iterate_phdr and _dl_mcount_wrapper_check
-- are stub in the forward map, not wired. There is nothing at this
face to run a differential against, so the slice's bar is the pin
instead: t/real-map.sh asserts gen-wire finds nothing to wire, that no
public dlfcn entry has crept into the libc map, and that the two
libc-resident dl names stay stub -- a dlopen wrongly added to libc is
caught the same way a drift in any wired slice is. The dl surface
itself is judged where it is implemented, in WP-54's libdl.

## The unassigned slice

The dl slice was one of the census's two the generator never wires; the
unassigned residue is the other, and it closes the same way -- by
accounting for every row and pinning that account, not by producing a
body. `cut-slices.py` credits a wired function to the header that
declares it, and 275 of the forward map's wired function rows are
declared by no header the map scans, so they land in no slice.
`t/real-map.sh` has always counted them; this increment certifies what
they are. 231 are the underscore-reserved internals glibc exports but no
public header declares -- the `__cxa_*`, `__*_chk`, and leading-underscore
call spellings the demand census ranks among the most-wanted bindings of
all, wired by their own forward-map rows and reached by name rather than
through a slice. 43 are the SunRPC `xdr*` family, whose headers el8 moved
out of glibc into libtirpc, so no glibc header the map scans declares them
though libc still exports the bodies. 1 is `gets`, which el8's C11
`stdio.h` no longer declares and a `_GNU_SOURCE` scan therefore never
sees. The three residues sum to 275 with nothing left over, and the pin
now asserts each count, not only the total and a membership rule, so a
name shifting from one residue to another -- an `xdr` entry retired, an
internal newly declared by a header -- is a conscious re-pin, the same
way a public name falling in already was. With this, both slices the
census leaves unwired are accounted for at the libc face; what remains of
WP-56 is its overall done-when, which needs the runtime the wired slices
wait on.


## The first filled stub: the ctype tables

Every slice above wires forwards (thunks) and shims (translations over a real
Cygwin export). The acceptance harness's first leaf, bzip2, needs one thing
neither of those covers: `__ctype_b_loc`, glibc's character-class accessor,
and its two case-map kin. They are bucket-4 stubs -- glibc-internal, absent
from Cygwin's export surface, nothing to forward to and nothing to translate.
But what they return is determinate: in the C and POSIX locales the three
tables they front are fixed by the language, so the veneer fills the stub with
the table rather than leaving a body that only fails. This is a fourth kind of
wiring body beside the thunk and the shim, and the decision recording when it
is allowed -- determinate data the veneer can produce and certify without a
Cygwin call behind it -- is its own DR; a stub whose answer needs state the
veneer lacks stays a stub that fails.

`gen-ctype-table.py` emits `ctype-table.gen.c` -- the three tables synthesized
from the standard's class and case rules, indexed `[-128..255]`, bound to
their `GLIBC_2.3` accessors -- as a shared component beside `xlat-core.gen.c`,
not in any slice's bind table, since it forwards to nothing. `t/ctype-table.sh`
pins it byte-identical to its generator and, over the pinned el8 image,
compiles a dumper against glibc's own `ctype.h` and a probe against this body
and requires the two identical across all 384 entries of all three tables:
certified byte-for-byte against the real reference, not merely by construction.
The behavioural surface is already `diff/locale/ctype.c`'s, whose candidate
side resolves through this body once the runtime links it. `CTYPE-TABLE-SHIM.md`
records the layout, the negative-index and EOF rule, the C-locale scope, and
the one follow-up: the acceptance harness still reads the classification, which
still says stub, so bzip2's verdict does not yet reflect the fill.

## The string slice: live crossing

math and runtime were the two slices whose whole table is NOSIGFE, and their
live crossings leaned on that. string is the first crossed slice that is not
wholly NOSIGFE, and it carries one shim among its forwards, so it tests two
things the earlier crossings could not: whether the bind loop tells a forward
from a shim against a real DLL, and whether NOSIGFE alone is enough to call a
body from the freestanding harness.

The bind answer is exact. Of the slice table's 43 distinct export names, 42 are
forwards, and the resolver -- the same PE-export walk live-math uses -- finds
every one of them in `elfsysv1.dll`. The forty-third, `__errno_location`, is
the slice's only shim: a name the veneer translates rather than forwards, and
one Cygwin exports no symbol for, so it does not resolve. The specimen asserts
that shape directly -- exactly one slot null, and that slot the shim's -- not
`missing == 0`, which would be false here and would erase the forward/shim
distinction the slice is built on.

The NOSIGFE answer is a boundary, and writing the specimen found it the hard
way. The first draft called memcpy and strverscmp, both NOSIGFE. Both returned
garbage, and called enough times both corrupted the specimen's own later
results: their bodies read Cygwin's reentrancy and thread-local state through a
thread pointer this freestanding harness never established. NOSIGFE names the
calling convention a thunk needs, not whether the body behind it stands on its
own; a reent-touching body needs the same process bring-up
`runtime/face/t/fault.c` performs that the SIGFE-fenced slices already wait on.
The thunks the specimen keeps -- ffs, ffsl, ffsll -- are pure register bit
scans with no memory, reent, or locale behind them, and they cross cleanly:
run through WP-41's branch against the real DLL, the bind resolves the table,
and all three return the real body's answer, including a second pass of all
three after a dozen crossings to show the family stays correct rather than
degrading -- status 15, the only pass, with the same refuse-before-entry and
no-runtime controls the earlier crossings use. `t/live-string.sh` records it.
The rest of string's reent-touching rows, memcpy and strverscmp among them,
are left for that process bring-up, and WP-56's overall done-when is unchanged:
still the vendor package's own test suite, run and passed.

## The stdlib slice: live crossing

string proved a shim's null slot and a scalar return; stdlib is the fifth
crossing and adds the return shape the earlier four could not. Its 97 rows
are all forwards with no shim, so the bind check is math's -- every row must
resolve, `missing == 0` -- not string's exactly-one-null. What the slice
carries that the others did not is `div`, `ldiv` and `lldiv`: functions that
return `div_t`, `ldiv_t` and `lldiv_t` by value. The psABI hands those back
in registers -- the eight-byte `div_t` in `%rax`, the two sixteen-byte
structs in `%rax:%rdx` -- and a thunk is a bare tail jump, so it forwards
that pair untouched. The specimen is the first to prove the pair survives
the bind loop and the branch: `div(17,5)`, `div(-17,5)`, `ldiv(100,7)` and
`lldiv(1000003,7)` all return both fields at C's truncation-toward-zero
answer.

The NOSIGFE boundary string found still holds and still bounds what the
crossing may call. `abs`, `labs`, `llabs`, `div`, `ldiv` and `lldiv` are
NOSIGFE (`runtime/exports/cygwin-exports.tsv`) and, like string's `ffs`
family, stand alone -- each is pure arithmetic over its arguments with no
memory, reent or locale behind it -- so they cross a freestanding harness
that never established Cygwin's thread pointer. The reent-touching rest of
stdlib -- `strtol` and the conversions, the environment, the seeded
generators, `qsort` -- is left for the same process bring-up
`runtime/face/t/fault.c` performs that the SIGFE-fenced slices already wait
on, exactly as string left `memcpy` and `strverscmp`.

Five checks, one bit each: the all-forward bind, then `abs`, `labs`,
`llabs`, and the div family with a second pass of the whole set after the
crossings to show the bodies stay correct rather than degrading -- status
31, the only pass, with the same refuse-before-entry and no-runtime
controls the earlier crossings use. `t/live-stdlib.sh` records it. WP-56's
overall done-when is unchanged: still the vendor package's own test suite,
run and passed.

## The sockets slice: live crossing

stdlib was all forwards with a struct-by-value return; sockets is the sixth
crossing and adds a weak-alias thunk. Its 66 rows are all forwards with no
shim, the largest all-forward table bound live so far, so the bind check is
math's and stdlib's -- every row must resolve, `missing == 0` -- not string's
exactly-one-null, and the bind loop resolves all 66 against the real
`elfsysv1.dll` with none missing.

What the slice carries that the others did not is the byte-order family.
glibc exports `ntohl` and `ntohs` as weak aliases of `htonl` and `htons` --
on a little-endian target the two are the same permutation over the same body
-- and `gen-wire.py` carries that weakness through: `htonl` (w00028) and
`htons` (w00029) are `.globl` thunks, `ntohl` (w00044) and `ntohs` (w00045)
are `.weak` ones. The specimen is the first to call a weak thunk directly and
show it reaches the same real body its strong twin does, agreeing value for
value. The family also returns narrower than a register -- `htons` and
`ntohs` hand back `uint16_t`, `htonl` and `ntohl` `uint32_t` -- so the caller
reads only the defined low bits of whatever the thunk forwards, a return
shape the earlier scalar-returning crossings did not exercise. The
self-inverse check closes it without assuming an endianness in the assertion:
`ntohl(htonl(v)) == v` and `htons(ntohs(w)) == w`, the pair composing back to
identity.

The NOSIGFE boundary string found still holds and still bounds what the
crossing may call. `htonl`, `htons`, `ntohl` and `ntohs` are NOSIGFE
(`runtime/exports/cygwin-exports.tsv`) and, like string's `ffs` family and
stdlib's `abs` family, stand alone -- each is a pure byte permutation over
its argument with no memory, reent or locale behind it -- so they cross a
freestanding harness that never established Cygwin's thread pointer. The
reent-touching rest of sockets -- the resolver, the socket calls, everything
whose body reads reent or touches a descriptor -- is left for the same
process bring-up `runtime/face/t/fault.c` performs that the SIGFE-fenced
slices already wait on, exactly as string left `memcpy` and `strverscmp`.

Five checks, one bit each: the all-forward bind, then `htonl`, `htons`, the
weak `ntohl`/`ntohs` aliases agreeing with their strong twins, and the
self-inverse round trips with a second pass of the whole set after the
crossings to show the bodies stay correct rather than degrading -- status 31,
the only pass, with the same refuse-before-entry and no-runtime controls the
earlier crossings use. `t/live-sockets.sh` records it. WP-56's overall
done-when is unchanged: still the vendor package's own test suite, run and
passed.

## The locale slice: live crossing

sockets was all forwards with a weak-alias thunk; locale is the seventh
crossing and the first of a slice whose family is almost entirely
off-limits to a freestanding harness. Its 83 rows are all forwards with no
shim, so the bind check is math's, stdlib's and sockets's -- every row must
resolve, `missing == 0` -- not string's exactly-one-null, and the bind loop
resolves all 83 against the real `elfsysv1.dll` with none missing.

What locale is, that the earlier crossed slices were not, is a slice whose
bodies overwhelmingly read locale or ctype state through the thread pointer
this harness never establishes. The classifiers and their `_l` twins,
setlocale, localeconv, nl_langinfo, strfmon and the message catalogs all
reach the current locale object or the ctype tables it fronts; calling any
of them here would corrupt the way string's memcpy and strverscmp did, and
for the same reason -- NOSIGFE names the calling convention a thunk needs,
not whether the body behind it stands on its own. So the slice with the
largest still-uncrossed family contributes exactly one live row.

That row is toascii (w00067). POSIX fixes it as `c & 0x7f` -- a pure
argument-only mask with no table, reent or locale behind it -- and
`runtime/exports/cygwin-exports.tsv` marks it NOSIGFE, so it crosses a
freestanding harness cleanly, exactly as string's `ffs` family, stdlib's
`abs` family and sockets's byte-order family did. It is the whole of what
locale offers a specimen that cannot set up a thread pointer, and the rest
of the slice is left for the process bring-up the SIGFE-fenced slices
already wait on, exactly as string left memcpy and strverscmp.

Five checks, one bit each: the all-forward bind, then toascii stripping bit
7 from a value that carries it, passing a value already inside 0..127
through untouched, masking EOF and negative inputs by their low seven bits
rather than treating them as errors (`(-1)&0x7f` is `0x7f`, `(-128)&0x7f` is
`0`), and finally agreeing with `c & 0x7f` over every c in -128..255 -- the
full range re-checked after the crossings above, so a body that had degraded
would show it there. Status 31 is the only pass, with the same
refuse-before-entry and no-runtime controls the earlier crossings use.
`t/live-locale.sh` records it. WP-56's overall done-when is unchanged: still
the vendor package's own test suite, run and passed.

## The posix slice: live crossing

locale was a slice whose family is almost entirely off-limits to a
freestanding harness; posix is the eighth crossing and the largest
all-forward table bound live so far. Its 108 rows -- 102 distinct export
names, all forwards with no shim -- resolve against the real `elfsysv1.dll`
with none missing, so the bind check is math's, stdlib's, sockets's and
locale's shape, every row filled and `missing == 0`, not string's
exactly-one-null. sockets at 66 rows held the previous largest; posix is
bigger, and binding it shows the resolver's PE-export walk scales past that
without a miss.

What posix is, that the earlier crossed slices were not, is a slice whose
bodies overwhelmingly touch a descriptor, the process, or Cygwin's reent and
thread state: fork, close, dup, access, the exec family, chdir, the pathconf
and getcwd calls all reach the kernel face or the current process through the
thread pointer this harness never establishes. Calling any of them here would
corrupt the way string's memcpy and strverscmp did, and for the same reason
-- NOSIGFE names the calling convention a thunk needs, not whether the body
behind it stands on its own. Even most of posix's own NOSIGFE rows -- getpid,
getuid, getlogin and their kin -- read process or cygheap state and are
off-limits to a specimen that cannot set up a thread pointer.

swab (w00089) is the one row that stands entirely alone. It is a byte
permutation between two caller-provided buffers -- it copies floor(n/2) pairs
from `from` to `to`, swapping the two bytes of each pair -- with no table,
reent, descriptor or locale behind it, and `runtime/exports/cygwin-exports.tsv`
marks it NOSIGFE. So it crosses a freestanding harness cleanly, exactly as
string's ffs family, stdlib's abs family, sockets's byte-order family and
locale's toascii did. The rest of posix is left for the process bring-up
`runtime/face/t/fault.c` performs that the SIGFE-fenced slices already wait
on.

What swab adds that the earlier crossed rows did not is its result shape. ffs,
abs, htonl and toascii each hand their answer back in a register; swab returns
void and writes its whole result through the caller's destination pointer.
This is the first crossed row whose output lands in caller memory rather than
in `%rax`, so it exercises a pointer-out argument class the earlier scalar and
struct-by-value returns never reached. The specimen proves that by handing
swab a destination buffer and reading the swapped bytes back from it: `ABCD`
becomes `BADC` over an even length, and a 256-byte buffer matches a local
reference swap pair for pair.

Writing the specimen found where the real body parts from POSIX. POSIX leaves
swab to do nothing when its length is not positive; the crossing confirmed
Cygwin's body does nothing for a zero length -- the destination keeps its
sentinel -- but for a negative length it does not return early, it writes
through the destination instead. That divergence is real, not the harness's:
a negative count is out of this crossing's scope, and pinning it is a job for
whatever differential later records the slice's divergences against a real el8
userland. The crossing keeps to swab's well-defined range -- a positive even
length, a positive odd length, and zero -- where the real body and the
standard agree.

Five checks, one bit each: the all-forward bind, then the even-length swap
through the destination pointer, an odd length that swaps the whole pairs and
leaves the trailing unpaired byte untouched, a zero length that writes
nothing, and swab against the local reference swap over a whole 256-byte
buffer, the last run after the crossings above so a body that had degraded
would show it there. Status 31 is the only pass, with the same
refuse-before-entry and no-runtime controls the earlier crossings use.
`t/live-posix.sh` records it. WP-56's overall done-when is unchanged: still
the vendor package's own test suite, run and passed.

## The time slice: live crossing

posix was the largest all-forward table bound live and the first crossed
row whose result landed in caller memory; time is the ninth crossing and the
first crossed forward whose answer comes back in a floating-point register.
Its 40 rows are all forwards with no shim, so the bind check is math's,
stdlib's, sockets's, locale's and posix's shape -- every row must resolve,
`missing == 0` -- not string's exactly-one-null, and the bind loop resolves
all 40 against the real `elfsysv1.dll` with none missing.

What time is, that the earlier crossed slices were not, is a slice whose
bodies overwhelmingly read a clock, a timezone, or Cygwin's reent and thread
state: clock_gettime, the gmtime/localtime family and their `_r` twins,
mktime, strftime, tzset and the timer calls all reach the kernel face, the tz
rules, or the current thread through the thread pointer this freestanding
harness never establishes. Calling any of them here would corrupt the way
string's memcpy and strverscmp did, and for the same reason -- NOSIGFE names
the calling convention a thunk needs, not whether the body behind it stands
on its own. timegm, the slice's other NOSIGFE row, reads the tz-independent
conversion tables and is not argument-only, so it is off-limits here too.

difftime (w00015) is the one row that stands entirely alone. POSIX and
Cygwin's body both fix it as the arithmetic difference of two calendar times
returned as a double -- on this target time_t is a signed integer and the
body is `(double)(time1 - time0)`, with no table, clock, reent or locale
behind it -- and `runtime/exports/cygwin-exports.tsv` marks it NOSIGFE. So it
crosses a freestanding harness cleanly, exactly as string's ffs family,
stdlib's abs family, sockets's byte-order family, locale's toascii and
posix's swab did. The rest of time is left for the process bring-up
`runtime/face/t/fault.c` performs that the SIGFE-fenced slices already wait
on.

What difftime adds that the earlier crossed rows did not is its result class.
ffs, abs, htonl and toascii each returned an integer in `%rax` and swab wrote
its result through a caller pointer; difftime returns a double, handed back
in `%xmm0` by the psABI, from two integer time_t arguments. It is the first
crossed forward whose inputs are integers in the general registers and whose
answer comes back in a floating-point register. math's own crossing returned
doubles, but math was the all-NOSIGFE slice that proved the bind mechanism;
difftime is the first of the demand-ranked, reent-heavy slices to exercise
the xmm return path. The crossing keeps to values every one of which is
exactly representable as a double -- small integers and a 10^9-second span,
all well under 2^53 -- so the checks compare for exact equality without a
tolerance.

Five checks, one bit each: the all-forward bind, then difftime of two equal
instants returning 0.0, difftime(later, earlier) as the positive span,
difftime(earlier, later) as that span negated so the sign is the body's and
not the harness's, and difftime against a local `(double)(a - b)` reference
over a set of pairs including the 10^9-second span, the last run after the
crossings above so a body that had degraded would show it there. Status 31 is
the only pass, with the same refuse-before-entry and no-runtime controls the
earlier crossings use. `t/live-time.sh` records it. WP-56's overall done-when
is unchanged: still the vendor package's own test suite, run and passed.

## The misc slice: live crossing

time was the ninth crossing and the first whose answer returned in a
floating-point register; misc is the tenth, and the first whose crossed rows
edit a caller-owned structure through pointers rather than return a value.
Its 33 rows are all forwards with no shim, so the bind check is math's,
stdlib's, sockets's, locale's, posix's and time's shape -- every row must
resolve, `missing == 0` -- not string's exactly-one-null, and the bind loop
resolves all 33 against the real `elfsysv1.dll` with none missing.

misc is reached out of demand order, and the reason is the same one that
passed over stdio, filesystem, memory and signal before it: those rank above
misc, but none offers a row a freestanding harness can call. stdio's one
NOSIGFE forward, `cuserid`, reads the password database through the cygheap.
memory and signal have no NOSIGFE forward at all -- signal's argument-only
rows, the `sigsetops` family, are shims the veneer translates rather than
forwards with a generated thunk, so there is no bare label to call.
filesystem's NOSIGFE forwards each fail the standalone test a different way:
`alphasort` and `versionsort` compare through `strcoll` and `strverscmp`,
the locale- and reent-touching calls string already deferred; `fnmatch`
reads locale collation; `endmntent` closes a `FILE`; `umask` swaps a
process-global mask; and `fts_set`'s only success-path effect is a write into
an `FTSENT` whose layout the cross toolchain's glibc headers and the real
newlib DLL need not share -- the one dependency every earlier crossing was
built to avoid. process fails it too: Cygwin's `posix_spawnattr_t` is an
opaque pointer, so the `posix_spawnattr_get`/`set` accessors dereference a
handle `posix_spawnattr_init` must first allocate on the heap. misc is the
highest-demand slice left whose clean rows need none of that.

`insque` (w00016) and `remque` (w00019; see `wire-misc.gen.s` for the index
-> name mapping) are those rows. Both are the historical System V queue
primitives, and their whole contract is pointer arithmetic over a
caller-owned doubly-linked list: `insque(elem, prev)` splices `elem` in after
`prev`, `remque(elem)` unsplices it, each touching only the `q_forw` and
`q_back` words at the head of the caller's nodes. `runtime/exports/
cygwin-exports.tsv` marks both NOSIGFE, and there is no table, clock, reent,
locale or kernel behind either, so they cross a freestanding harness cleanly,
exactly as string's `ffs` family, stdlib's `abs` family, sockets's byte-order
family, locale's `toascii`, posix's `swab` and time's `difftime` did. The
queue node the specimen builds is its own struct with two leading pointer
words -- the `q_forw`, `q_back` layout the primitives' contract fixes for
every libc -- so, like swab's raw byte buffer and difftime's scalars, nothing
here rests on a header the two sides might lay out differently.

What the crossing adds that the earlier crossed rows did not is the shape of
what a wired body may do. `difftime` returned a double, `swab` wrote a byte
buffer the caller owned, the `abs` and byte-order families returned scalars in
`%rax`; `insque` and `remque` take two caller pointers, read one node to reach
its neighbour, and write links in both directions. The specimen builds a
three-node queue with `insque` -- head, then middle after head, then tail
after middle -- and then removes the middle with `remque`, checking after each
that the links point where the primitives' contract fixes them. Each node
starts with a sentinel in both link words, so a body that failed to write a
link would leave the sentinel and fail rather than pass by luck.

Five checks, one bit each: the all-forward bind, then the head linking
forward to the middle and back to nothing, the middle linking both ways --
forward to the tail, back to the head, the write `insque` makes in the
direction a naive splice would miss -- the tail linking forward to nothing
and back to the middle, and finally, after `remque` of the middle, the head
and tail closing over the gap, run last so a body that had degraded over the
crossings above would show it there. Status 31 is the only pass, with the
same refuse-before-entry and no-runtime controls the earlier crossings use.
`t/live-misc.sh` records it. WP-56's overall done-when is unchanged: still the
vendor package's own test suite, run and passed.


## The wchar slice: live crossing

misc was the tenth crossing and every one before it passed: the bind held and
the crossed rows came back correct. wchar is the eleventh and the first whose
finding is negative. Its 87 rows are all forwards with no shim, so the bind
check is math's, stdlib's, sockets's, locale's, posix's, time's and misc's
shape -- every row must resolve, `missing == 0`, not string's exactly-one-null
-- and it holds: all 87 wide names are exported by the real DLL, so a resolved
thunk reaches the real body. What the ten crossings before it went on to
confirm, and this one refutes, is that a resolved row then crosses as a plain
tail jump.

wmemcpy (w00082), wmemmove (w00083) and wmempcpy (w00084; see `wire-wchar.gen.s`
for the index -> name mapping) are the rows the specimen calls. All three are
the wide analogues of memcpy/memmove/mempcpy, marked NOSIGFE in
`runtime/exports/cygwin-exports.tsv`, standing on no conversion state, stream,
reent, locale or kernel -- exactly the freestanding shape string's ffs family,
stdlib's abs family, sockets's byte-order family, locale's toascii, posix's
swab, time's difftime and misc's insque/remque were chosen for. So the thunk
reaches the body cleanly. The body then moves by its own idea of how wide a
wchar_t is, and that idea is the finding.

The face this tree presents is el8's, where wchar_t is four bytes, the System V
width; the body inside `elfsysv1.dll` is Cygwin's newlib, whose wchar_t is two
bytes, the width Windows uses. The paragraph on the wchar wiring above recorded
the surface as value-preserving because its diff cases ran with both sides on
el8, glibc against glibc, both four bytes. Face against the real DLL,
`wmemcpy(dst, src, 5)` moves ten bytes, not twenty: five two-byte elements. A
caller that built its source as four-byte elements, as the face's wchar_t is,
hands the body five words it reads as ten narrow ones. The payload the specimen
uses sets the high bits of every element, so a two-byte move drops the high
half rather than coinciding with the four-byte result by luck, and every buffer
is the specimen's own array of a plain four-byte word, so what the checks read
is the body's own width and not a header the two sides lay out differently.

Five checks, one bit each, so 31 is the only pass -- and here a pass means the
negative finding holds and reproduces: the all-forward bind with every wide name
exported, then wmemcpy reaching the real body (its destination no longer the
sentinel), then the move matching a two-byte-wchar_t body exactly -- ten bytes
for five elements, the rest untouched -- and not the four-byte model the slice
assumed, then wmempcpy returning the destination advanced by ten bytes, 2 * n,
the body striding in its own element rather than the twenty the face's wchar_t
would give, and finally a second length, wmemcpy of three elements moving six
bytes, run last so a body that had drifted would show it there. Status 31 is the
only pass, with the same refuse-before-entry and no-runtime controls the earlier
crossings use. `t/live-wchar.sh` records it.

The consequence is a decision this crossing adds: the wchar slice is
reclassified from forward to shim, every wchar_t-bearing row needing a
width-translating shim -- narrowing four-byte face elements to the body's two on
the way down, widening on the way up, scaling counts and returned end pointers by
the ratio -- not a tail jump. The wire table's thunks stay in place only until
that shim generator replaces them. WP-56's overall done-when is unchanged: still
the vendor package's own test suite, run and passed.


## The terminal slice: live crossing

wchar was the eleventh crossing and the first whose finding was negative, and
it turned on a scalar width; terminal is the twelfth, and its finding is
negative too, one level up -- it turns on an aggregate's layout. Its thirty
rows are forwards but one shim (`ioctl`), so the bind check is the all-resolve
shape math, stdlib, sockets, posix, time, misc and wchar used -- every row must
resolve, `missing == 0`, not string's exactly-one-null -- and it holds: every
terminal name, `ioctl` included, is exported by the real DLL, so a resolved
thunk reaches the real body. What the crossings before it confirmed for their
rows, and this one refutes for the struct-reading rows, is that a resolved
terminal row crosses as a plain tail jump.

`cfgetispeed` (w00000), `cfgetospeed` (w00001) and `cfmakeraw` (w00002; see
`wire-terminal.gen.s` for the index -> name mapping) are the rows the specimen
calls. All three are marked NOSIGFE in `runtime/exports/cygwin-exports.tsv`,
standing on no reent, locale, table or kernel -- exactly the freestanding shape
string's `ffs` family, stdlib's `abs` family, misc's `insque`/`remque` and the
rest were chosen for. So the thunk reaches the body cleanly. The body then
reads or writes the caller's `struct termios` by its own idea of where the
fields lie, and that idea is the finding.

The face this tree presents is el8's, where `struct termios` is sixty bytes
with `NCCS` thirty-two; the body inside `elfsysv1.dll` is Cygwin's newlib,
whose `struct termios` is forty-four bytes with `NCCS` eighteen. The leading
flag words and the start of `c_cc` share their offsets exactly -- `c_iflag` at
0, `c_oflag` at 4, `c_cflag` at 8, `c_lflag` at 12, `c_cc` at 17 on both -- but
the shorter `c_cc` pulls `c_ispeed` and `c_ospeed` from the face's 52/56 to the
body's 36/40. `cfgetispeed` and `cfgetospeed` in the body are plain field reads
(`return t->c_ispeed;`, measured on Cygwin), so on a face-laid struct they read
the body's offset, fourteen bytes short of the face's field. The paragraph on
the terminal wiring above recorded the surface as value-preserving because its
diff cases ran with both sides on el8, glibc against glibc, both `NCCS`-32. Face
against the real DLL, `cfgetispeed` on a struct whose face `c_ispeed` was filled
returns whatever sits at the body's offset instead.

The specimen owns every buffer outright -- a plain byte array laid out by hand
to the face's offsets, never a libc `struct termios` -- so what the checks read
is the body's own placement against the face's and not a header the two sides
lay out for the compiler differently. It puts one marker at the face's speed
offset and a different one at the body's, so a body reading the face field
could not match the body-offset check by luck.

Five checks, one bit each, so 31 is the only pass -- and here a pass means the
negative finding holds and reproduces: the all-forward bind with every terminal
name exported, then `cfgetispeed` reaching the real body and reading the body's
`c_ispeed` at offset 36 (the value placed there, not the face's at 52), then
`cfgetospeed` reproducing the divergence on the output speed at offset 40, then
a control confirming the body never consults the face's offset -- a struct with
only the face fields set returns zero -- and finally `cfmakeraw` crossing the
shared leading region exactly, the four flag words coming back the body's
measured raw-mode values (`0xfffefa1c`, `0xfffffffe`, `0xfffffeff`,
`0xfffffed8`), run last so a body that had drifted on the shared region would
show it there. Status 31 is the only pass, with the same refuse-before-entry
and no-runtime controls the earlier crossings use. `t/live-terminal.sh` records
it.

The consequence is a decision this crossing adds: the terminal slice's
`struct termios`-bearing rows are reclassified from forward to shim, every row
that touches `c_cc` past its start, `c_ispeed` or `c_ospeed` needing a
layout-translating shim -- mapping the face's sixty-byte, `NCCS`-32 struct to
the body's forty-four-byte, `NCCS`-18 one on the way down and back on the way up
-- not a tail jump. `cfmakeraw` and any row confined to the shared leading flag
words may stay a forward; the crossing pins that those offsets coincide. The
wire table's thunks stay in place only until that shim generator replaces the
struct-reading rows. The finding generalizes: `struct termios` is one aggregate
the two libcs size differently and unlikely to be the last, so every later
struct-bearing slice must be crossed against the real DLL before its struct rows
are trusted as forwards. WP-56's overall done-when is unchanged: still the
vendor package's own test suite, run and passed.

## The stdio slice: live crossing

The thirteenth live crossing is the first that crosses a slice by its bind
alone. stdio is the highest-demand real slice -- rank three behind string and
the unassigned internals, 24513 in the demand census -- and the first whose
bodies are categorically beyond a freestanding caller. Its table is
`wire-stdio.gen.c`: 97 rows, no shim, all forwards. The bind holds -- all 88
distinct export names the rows reach (`fopen`, `vfprintf`, `fread`, and the
version and large-file aliases collapsed onto their base, `fopen64` onto
`fopen`, `fgetpos64` onto `fgetpos`, and their kin) are exported by the real
DLL, `missing` is zero.

What stdio does not offer is a body the specimen may enter. Every earlier
crossing exercised a body by choosing the slice's NOSIGFE forwards, the rows
standing on no reent, locale, table or kernel. stdio has none to choose. Of its
97 rows exactly one is marked NOSIGFE -- `cuserid` -- and `cuserid` is no pure
mask: its body reads the process's user through the cygheap this freestanding
specimen never brings up, so calling it would fault or read uninitialised state
the way calling `setlocale` would. The other 96 are SIGFE, wanting the
signal-frame entry the specimen compiles without, and the `printf`/`scanf`/
`FILE` families behind them stand on `_REENT` besides. `NOSIGFE` names the
calling convention a thunk needs, not whether the body behind it stands on its
own; locale's crossing kept one pure exception, `toascii`, and stdio keeps
none.

So the specimen calls nothing. It reads the table the bind filled and the DLL's
own PE header -- never a libc datum, never a generated thunk -- and certifies
the one thing a freestanding harness can certify of stdio against a real DLL:
that the bind is real and faithful. Five checks, one bit each, so 31 is the
only pass -- the all-row bind with `missing` zero; every filled slot landing
inside the DLL's mapped image span `[base, base + SizeOfImage)`, so a resolved
thunk tail-jumps into the real body region and not off into unmapped space; the
resolver discriminating, `fopen` resolving while the un-collapsed alias name
`fopen64` and a junk name resolve null, so `missing` zero is a fact about the
names; `fopen`, `fclose` and `vfprintf` reaching three distinct bodies; and a
second bind idempotent, `missing` zero again with every slot equal to a fresh
resolve, per DR-0049's contract. The same refuse-before-entry and no-runtime
controls the earlier crossings use bound it. `t/live-stdio.sh` records it.

The consequence is a decision this crossing adds: a SIGFE slice with no pure
NOSIGFE row crosses live by its bind alone, its bodies left to `diff-slice.sh`
on el8 and to process bring-up, neither of which is a freestanding harness's to
give. The SIGFE-heavy slices still uncrossed -- memory, signal, process,
identity, io-mux, threads, regex, syslog, sysv-ipc, io, system -- inherit the
rule: each presents its own worklist to the same question, and where the answer
matches stdio's the crossing is the bind and the finding is that the bodies
wait. This is where the freestanding-harness technique reaches its limit and
hands the bodies to the differential and to bring-up, which is where they were
always going to be certified. WP-56's per-slice done-when is unchanged: still
the differential against a real el8 userland, with the live crossing the added
NT check, and for a SIGFE slice that added check is the bind.

## The filesystem slice: live crossing

The fourteenth live crossing is the second crossed by its bind alone, and it
reaches that shape by measurement rather than by category. filesystem is the
highest-demand slice still uncrossed -- rank six, 13195 in the demand census --
and DR-0055, in listing the SIGFE-heavy slices that cross by their bind alone,
left filesystem off, on the reading that it carries a callable pure row. It has
NOSIGFE argument-only rows that look the part -- `fnmatch`, `alphasort`,
`versionsort` -- but none is stateless. `fnmatch` consults the locale's ctype
and collation; `alphasort` and `versionsort` run `strcoll` and `strverscmp`
over a `struct dirent`, standing on locale and on a header layout the crossing
discipline keeps out of a specimen. Calling `fnmatch` proved the point
directly: built byte-identical and run three times against the real DLL it
returned three different five-check verdicts, 30 then 5 then 31, the signature
of a body reading uninitialised state. `NOSIGFE` names the calling convention a
thunk needs, not whether the body behind it stands on its own -- DR-0055's own
words -- and filesystem is a slice with NOSIGFE rows and no stateless one. So it
crosses by its bind alone, as stdio did, and its bodies wait on `diff-slice.sh`
and process bring-up.

The bind carries a finding stdio's did not. Of the table's 103 rows, eleven do
not resolve against a Cygwin-faced DLL, and they are exactly the rows a real
shim must synthesise. Ten are the stat family: glibc's versioned wrappers
`__xstat`, `__fxstat`, `__lxstat`, `__xmknod`, their `*at` forms and their `*64`
forms -- the `(int version, ...)` entry points el8 binaries import, an ABI
Cygwin has no counterpart for. Cygwin exports the plain calls instead --
`stat`, `fstat`, `lstat`, `fstatat`, `mknod`, `mknodat`, all present in the DLL
-- and, being LP64 with a single 64-bit `off_t`, carries no separate `*64`
symbol. The eleventh is `getdirentries`, which Cygwin exports neither as itself
nor as `getdents`. The generator left the glibc name in each row's export_name
as a placeholder; a real shim body drops the version argument, translates the
`struct stat` layout and calls the Cygwin function, with the `*64` rows mapping
onto the same call and `getdirentries` composed from `readdir`/`seekdir`/
`telldir` or left a documented stub. Every other row -- every forward, and the
twenty-five shims whose export exists -- binds.

So the specimen calls nothing and reads the table the bind filled and the DLL's
own PE header. Five checks, one bit each, so 31 is the only pass -- the bind
leaving exactly those eleven rows null and every other row filled, the null set
identified by name so it is exactly the rows a shim must reach and no others;
every filled slot landing inside the mapped image span `[base, base +
SizeOfImage)`, the eleven expected-null slots skipped; the resolver
discriminating, `chmod` resolving while `__xstat` and a junk name resolve null,
so the all-but-eleven result is a fact about the names; `chmod`, `closedir` and
`fnmatch` reaching three distinct bodies; and a second bind idempotent, the same
eleven null again with every filled slot equal to a fresh resolve, per DR-0049's
contract. The same refuse-before-entry and no-runtime controls the earlier
crossings use bound it. `t/live-filesystem.sh` records it.

The consequence is a decision this crossing adds: filesystem crosses by its
bind alone -- extending DR-0055's rule to a slice that has NOSIGFE rows but no
stateless one -- and the stat family and `getdirentries` are wired to glibc
export names a Cygwin face does not carry, so those eleven rows are shim
placeholders the generator must repoint at the Cygwin calls a translating body
reaches. WP-56's per-slice done-when is unchanged: still the differential
against a real el8 userland, with the live crossing the added NT check, and for
this slice that added check is the bind.

The memory slice crosses next, and it is the first to cross with an empty
unresolved set. Its table is twenty-one rows, all forwards and no shim: the
allocator (`malloc`, `calloc`, `realloc`, `reallocarray`, `free`, `memalign`,
`valloc`) with its `mallopt`/`mallinfo`/`malloc_*` introspection family, and the
`mman` calls (`mmap`, `munmap`, `mprotect`, `msync`, `madvise`, `posix_madvise`,
`mlock`, `munlock`). Every name glibc exports here, Cygwin exports under the same
name -- the whole malloc introspection family included, though Cygwin's
allocator is its own. The one apparent gap is not one: glibc's `mmap64` is a
distinct versioned symbol and the bare name is absent from a Cygwin DLL, which
being LP64 has no separate `*64` export, but the `mmap64` row already carries
export_name `mmap`, a forward-alias onto the single call, so the row binds
through `mmap` and only the bare name -- which no row asks the resolver for --
is missing. So memory needs no shim at all, and the finding is the absence of
one: where filesystem left eleven rows for a shim to synthesise, memory leaves
none.

memory crosses by its bind alone all the same, not by call: no memory row is
stateless. `malloc` and `free` stand on the allocator's arena, its growth and
its locks; the `mman` calls are syscalls into the kernel's address space. A
freestanding harness brings none of that up, so a body called here would read or
mutate state the harness never initialised -- the trap `fnmatch` sprang in
filesystem. So the specimen calls nothing and reads the table the bind filled.
Five checks, one bit each, so 31 is the only pass -- the bind leaving no row
null, every filled slot landing inside the mapped image span, the resolver
discriminating while the `mmap64` alias holds (`malloc` resolves, the bare name
`mmap64` and a junk name resolve null, yet the `mmap64` row binds through its
`mmap` export_name), `malloc`/`free`/`mmap` reaching three distinct bodies, and
a second bind idempotent, per DR-0049's contract. The same refuse-before-entry
and no-runtime controls the earlier crossings use bound it. `t/live-memory.sh`
records it. The crossing adds no decision: it confirms DR-0055's rule on a slice
that has NOSIGFE rows but no stateless one, and finds the pure-forward case that
needs no shim placeholder repointed, so there is nothing for it to change.


The signal slice crosses next, the third crossed by its bind alone. Its table
is twenty-nine rows, twelve forwards and seventeen shims: the sigset operators
(`sigemptyset`, `sigfillset`, `sigaddset`, `sigdelset`, `sigismember`) and the
disposition and delivery calls (`signal`, `sigaction`, `sigprocmask`,
`sigpending`, `sigsuspend`, `sigqueue`, `sigtimedwait`, `sigwaitinfo`,
`sigaltstack`) as shims, with `kill`, `killpg`, `raise`, `psignal` and the
System V XSI conveniences (`sighold`, `sigrelse`, `sigignore`, `sigset`,
`sigpause`) as forwards. Every row is SIGFE and none is stateless, so the
crossing asks the question memory and filesystem did: does a Cygwin-faced DLL
export the whole set, or does the bind leave rows a shim body must synthesise?

Measurement answers cleanly, and it places signal between filesystem's eleven
and memory's none: the bind leaves exactly two rows null, and they are exactly
the rows a real shim must synthesise -- `__sysv_signal` and `sysv_signal`, the
System V unreliable-signal disposition setters, which glibc exports but Cygwin
has no ABI for. Cygwin exports plain `signal` with BSD reliable semantics and
`sigaction`, and a translating body must build the System V one-shot, no-mask
disposition on top of them; there is no export to alias onto, as memory's
`mmap64` aliased onto `mmap`. Every other row -- every forward, and the fifteen
shims whose export exists under its own name, the sigset operators and the
delivery calls alike -- binds.

signal crosses by its bind alone all the same, not by call: no signal row is
stateless. The sigset operators look pure -- `sigemptyset` only clears a
caller's mask -- but Cygwin's are SIGFE, entering the runtime's `cygtls` on the
way in, and a freestanding harness never brings that up; the delivery calls
stand on the process's signal state outright. So a body called here would read
or mutate state the harness never initialised -- the trap `fnmatch` sprang in
filesystem. So the specimen calls nothing and reads the table the bind filled.
Five checks, one bit each, so 31 is the only pass -- the bind leaving exactly
the two System V rows null, identified by name so it is exactly the rows a shim
must reach and no others, and every other row filled; every filled slot landing
inside the mapped image span `[base, base + SizeOfImage)`; the resolver
discriminating, `sigaction` resolving while `__sysv_signal` and a junk name
resolve null; `sigaction`, `sigprocmask` and `kill` reaching three distinct
bodies; and a second bind idempotent, the same two null again with every filled
slot equal to a fresh resolve, per DR-0049's contract. The same
refuse-before-entry and no-runtime controls the earlier crossings use bound it.
`t/live-signal.sh` records it.

The consequence is the same shape as filesystem's: signal crosses by its bind
alone, confirming DR-0055's rule on a slice that has NOSIGFE rows but no
stateless one, and the two System V disposition rows are wired to glibc export
names a Cygwin face does not carry, so they are shim placeholders the generator
must repoint at a translating body over Cygwin's `signal`/`sigaction` rather
than at a plain Cygwin call. WP-56's per-slice done-when is unchanged: still the
differential against a real el8 userland, with the live crossing the added NT
check, and for this slice that added check is the bind.


The process slice crosses next, the fourth crossed by its bind alone. Its table
is forty-three rows, thirty-nine forwards and four shims: the wait family
(`wait`, `wait3`, `wait4`, `waitpid`), the POSIX spawn surface (`posix_spawn`,
`posix_spawnp` and the `posix_spawn_file_actions_*` and `posix_spawnattr_*`
families), the scheduler calls (`sched_yield`, `sched_getparam`,
`sched_setscheduler`, `sched_getaffinity` and its two versions, and the rest of
`sched_*`), the priority pair (`getpriority`, `setpriority`) as forwards, and
the resource limits (`getrlimit`, `getrlimit64`, `setrlimit`, `setrlimit64`) as
the four shims. Every row is SIGFE and none is stateless, so the crossing asks
the question memory and signal did: does a Cygwin-faced DLL export the whole
set, or does the bind leave rows a shim body must synthesise?

Measurement answers cleanly, and it puts process alongside memory rather than
signal: the bind leaves no row null. Every process export glibc names, Cygwin
exports under the same name. The four shims are the apparent gap and are not
one. glibc splits the resource-limit calls into a base and an LFS `*64` variant;
Cygwin, being LP64, has one call each and no separate `getrlimit64` or
`setrlimit64` export, so the bare `*64` names are absent from the DLL. But the
generator already knew that: the `getrlimit64` row carries export_name
`getrlimit` and the `setrlimit64` row `setrlimit`, forward-aliases onto the
single call, so those rows bind through the base name and only the unused bare
`*64` names -- which no row asks the resolver for -- are missing. This is
memory's `mmap64` case a second time, and on a 64-bit target the `*64` alias is
the base call unchanged, no translation left to do. So process needs no shim,
and the finding is the absence of one: where signal left two System V rows for a
shim to synthesise, process leaves none.

process crosses by its bind alone all the same, not by call: no process row is
stateless. `wait` and `waitpid` stand on the process's children and its signal
state; `posix_spawn` forks and execs; the scheduler and rlimit calls are
syscalls into the kernel's per-process state, and every one is SIGFE, entering
the runtime's `cygtls` on the way in. A freestanding harness brings none of that
up, so a body called here would read or mutate state the harness never
initialised -- the trap `fnmatch` sprang in filesystem. So the specimen calls
nothing and reads the table the bind filled. Five checks, one bit each, so 31 is
the only pass -- the bind leaving no row null, every filled slot landing inside
the mapped image span `[base, base + SizeOfImage)`, the resolver discriminating
while the `getrlimit64` alias holds (`getrlimit` resolves, the bare name
`getrlimit64` and a junk name resolve null, yet the `getrlimit64` row binds
through its `getrlimit` export_name), `waitpid`/`posix_spawn`/`sched_yield`
reaching three distinct bodies, and a second bind idempotent, per DR-0049's
contract. The same refuse-before-entry and no-runtime controls the earlier
crossings use bound it. `t/live-process.sh` records it. The crossing adds no
decision: it confirms DR-0055's rule on a slice that has NOSIGFE rows but no
stateless one, and finds the pure-bind case that needs no shim placeholder
repointed, the LFS `*64` rows already aliased onto their base exports. WP-56's
per-slice done-when is unchanged: still the differential against a real el8
userland, with the live crossing the added NT check, and for this slice that
added check is the bind.

The identity slice crosses next, the fifth crossed by its bind alone and the
plainest no-shim case yet. Its table is seventeen rows, all forwards and no
shim: the passwd database (`getpwent`, `getpwnam`, `getpwuid` and their `_r`
forms, `setpwent`, `endpwent`), the group database (`getgrent`, `getgrnam`,
`getgrgid` and their `_r` forms, `setgrent`, `endgrent`, `getgrouplist`), and
the two supplementary-group calls (`initgroups`, `setgroups`). Every row is a
forward-same: the name glibc versions at `GLIBC_2.2.5` is the name Cygwin
exports, and every row's export_name is its own plain symbol. So the crossing
asks memory's question and gets memory's answer in its plainest form: does a
Cygwin-faced DLL export the whole set, or does the bind leave rows a shim must
synthesise? Measurement leaves no row null. Where process's four `getrlimit64`
and `setrlimit64` rows looked like a gap until their export_name aliases closed
it, identity has no such gap to explain away -- no LFS `*64` variant to
reconcile, no System V disposition as signal had, no struct translation as
filesystem's stat family had. The finding is the absence of a shim with nothing
standing in for one.

identity crosses by its bind alone all the same, not by call: no identity row is
stateless. The passwd and group readers walk `/etc/passwd`, `/etc/group` and
Cygwin's account mapping and keep an enumeration cursor across
`getpwent`/`getgrent`; `initgroups` and `setgroups` mutate the process's
supplementary-group set; every one is SIGFE, entering the runtime's `cygtls` on
the way in. A freestanding harness brings none of that up, so a body called here
would read or mutate account state the harness never initialised -- the trap
`fnmatch` sprang in filesystem. So the specimen calls nothing and reads the
table the bind filled. Five checks, one bit each, so 31 is the only pass -- the
bind leaving no row null, every filled slot landing inside the mapped image span
`[base, base + SizeOfImage)`, the resolver discriminating while the no-alias
finding holds (`getpwnam` resolves, a junk name resolves null, and the row whose
export_name is `getpwnam` bound to exactly that export),
`getpwnam`/`getgrgid`/`initgroups` reaching three distinct bodies, and a second
bind idempotent, per DR-0049's contract. The same refuse-before-entry and
no-runtime controls the earlier crossings use bound it. `t/live-identity.sh`
records it. The crossing adds no decision: it confirms DR-0055's rule on a slice
that is SIGFE throughout yet needs no shim placeholder repointed at all, every
row already a plain forward onto its own export. WP-56's per-slice done-when is
unchanged: still the differential against a real el8 userland, with the live
crossing the added NT check, and for this slice that added check is the bind.

The io-mux slice crosses next, the sixth crossed by its bind alone and the
eighteenth crossing overall. Its table is eight rows, all forwards and no
shim: the readiness multiplexers (`poll`, `ppoll`, `pselect`, `select`) and
the descriptor-backed event sources (`signalfd`, and the timerfd trio
`timerfd_create`, `timerfd_gettime`, `timerfd_settime`). The one thing new,
small and worth naming, is version spread. identity's seventeen rows all
versioned at `GLIBC_2.2.5`; io-mux's eight carry four tags -- `poll`,
`pselect` and `select` at `GLIBC_2.2.5`, `ppoll` at `GLIBC_2.4`, `signalfd` at
`GLIBC_2.7`, and the timerfd trio at `GLIBC_2.8`. The tag lives in the thunk's
`.symver`, not in the export name the bind resolves, so every row's export_name
is still its own plain symbol and the spread changes nothing for the crossing.
So the crossing asks memory's question and gets memory's answer in its plainest
form across a mixed-version slice: does a Cygwin-faced DLL export the whole set,
or does the bind leave rows a shim must synthesise? Measurement leaves no row
null. Where process's four `getrlimit64` and `setrlimit64` rows looked like a
gap until their export_name aliases closed it, io-mux has no such gap to explain
away -- no LFS `*64` variant to reconcile, no System V disposition as signal
had, no struct translation as filesystem's stat family had.

io-mux crosses by its bind alone all the same, not by call: no io-mux row is
callable from a freestanding harness. `poll`, `ppoll`, `pselect` and `select`
block on real descriptors and a timeout no event loop here satisfies; `signalfd`
and the timerfd trio create descriptor-backed kernel objects and are SIGFE,
entering the runtime's `cygtls` on the way in. A freestanding harness brings
none of that up, so a body called here would touch descriptor and signal-mask
state the harness never initialised -- the trap `fnmatch` sprang in filesystem.
So the specimen calls nothing and reads the table the bind filled. Five checks,
one bit each, so 31 is the only pass -- the bind leaving no row null, every
filled slot landing inside the mapped image span `[base, base + SizeOfImage)`,
the resolver discriminating while the no-alias finding holds (`poll` resolves, a
junk name resolves null, and the row whose export_name is `poll` bound to
exactly that export), `poll`/`select`/`signalfd` reaching three distinct bodies
across three of the slice's four version tags, and a second bind idempotent, per
DR-0049's contract. The same refuse-before-entry and no-runtime controls the
earlier crossings use bound it. `t/live-io-mux.sh` records it. The crossing adds
no decision: it confirms DR-0055's rule on a version-spread slice, every row a
plain forward onto its own export with the four `.symver` tags playing no part
in the resolution. WP-56's per-slice done-when is unchanged: still the
differential against a real el8 userland, with the live crossing the added NT
check, and for this slice that added check is the bind.

The threads slice crosses next, the seventh crossed by its bind alone and the
nineteenth crossing overall. Its table is 42 rows -- 41 forwards and one shim.
The forwards are the POSIX threads surface `pthread.h` and `semaphore.h` name
(the attribute, condition-variable, mutex and scheduling families,
`pthread_self`, `pthread_equal`, `pthread_exit`, the cancellation setters) and
the four C11 `threads.h` entries (`thrd_current`, `thrd_equal`, `thrd_sleep`,
`thrd_yield`). Two things are new here, neither a System V gap. The one shim is
a setjmp-family rename: its thunk is `__sigsetjmp`, `setjmp.h`'s versioned entry
at `GLIBC_2.2.5`, but its export_name is the plain `sigsetjmp`, so it binds onto
Cygwin's own `sigsetjmp` export like a forward -- unlike signal's two System V
rows, which had no Cygwin export at all and left the table null for a shim to
synthesise. threads' shim is a name the DLL already carries; signal's were names
it did not. And the version spread here is a compat pair, not distinct names:
the six `pthread_cond_*` entries (`broadcast`, `destroy`, `init`, `signal`,
`timedwait`, `wait`) each carry two rows, one at `GLIBC_2.2.5` and one at
`GLIBC_2.3.2`, the compat pair glibc shipped when the condvar ABI changed.
Where io-mux's spread put four tags on eight distinct names, threads puts two
tags on one name twice over.

The tag lives in the thunk's `.symver`, not in the export name the bind
resolves, so both rows of a pair resolve to the single Cygwin export of that
plain name and the bind fills both. So the crossing asks memory's question and
gets memory's answer across a slice with a rename and a compat pair: does a
Cygwin-faced DLL export the whole set, or does the bind leave rows a shim must
synthesise? Measurement leaves no row null -- no System V disposition as signal
had, no struct translation as filesystem's stat family had. threads crosses by
its bind alone all the same, not by call: every `pthread` and `thrd` body is
SIGFE, entering the runtime's `cygtls` and its thread registry on the way in,
and `sigsetjmp` saves a signal mask against machinery a freestanding harness
never brought up -- the trap `fnmatch` sprang in filesystem. So the specimen
calls nothing and reads the table the bind filled. Five checks, one bit each, so
31 is the only pass -- the bind leaving no row null, every filled slot landing
inside the mapped image span `[base, base + SizeOfImage)`, the resolver
discriminating while the no-alias finding holds (`pthread_self` resolves, a junk
name resolves null, and the row whose export_name is `pthread_self` bound to
exactly that export), `pthread_self`/`pthread_mutex_lock`/`sigsetjmp` reaching
three distinct bodies, and a second bind idempotent, per DR-0049's contract. The
same refuse-before-entry and no-runtime controls the earlier crossings use
bound it. `t/live-threads.sh` records it. The crossing adds no decision: it
confirms DR-0055's rule on a slice carrying a rename and a compat pair, every
row a plain forward onto its own export with the `.symver` tags playing no part
in the resolution. WP-56's per-slice done-when is unchanged: still the
differential against a real el8 userland, with the live crossing the added NT
check, and for this slice that added check is the bind.
