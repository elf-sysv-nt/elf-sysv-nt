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
