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
reports the slice empty (WIP: the pin below and this paragraph land
together). The two names the slice map does place in libc --
dl_iterate_phdr and _dl_mcount_wrapper_check -- are stub in the forward
map, not wired.
