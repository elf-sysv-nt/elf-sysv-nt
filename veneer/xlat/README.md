# The translation tables (WP-55)

The divergence classes DR-0000 names, as generated tables rather than as
knowledge in someone's head. DR-0000 commits the veneer to presenting
Linux's ABI exactly — errno values, signal numbers, struct layouts — and
translating up from Cygwin; these tables are the exact statement of what
that translation has to do. Each is extracted mechanically from the two
header trees and committed with a reproduce test in the WP-51 manner, so
none of it can drift into folklore.

## The two sides

The Linux side is el8's own header surface: the vendored
`glibc-headers-2.28` set in `veneer/include/` (WP-50, DR-0000's copy
line) over the pinned `kernel-headers-4.18.0-553.el8_10` package, which
glibc's headers defer to for errno values and flag bits. The kernel
headers are fetched and checksum-pinned by `fetch-kernel-headers.sh`,
never vendored (DR-0002); the cache lives in gitignored `a/vendor/xlat`
beside the main `.git`, shared by every worktree.

The Cygwin side is the WP-26 `newlib-cygwin` tree at `b11613e47`
(DR-0007): `winsup/cygwin/include` over `newlib/libc/include`, the same
header stack `elfsysv1.dll` is compiled from.

## How the extraction reads them

`extract-tables.py` compiles probes with the native gcc and never runs
target code. Constant values come from the assembly of
`const unsigned long long` initializers (`gcc -S`, parsed for `.quad`);
struct layouts come from DWARF (`gcc -g -c`, `objdump --dwarf=info`),
member by member, with anonymous aggregates flattened and array sizes
computed from their bounds.

The name universe is discovered, not curated: macro names are taken from
the Linux side's `gcc -E -dM` output by family pattern — errno names
from `errno.h`'s own expansion, signal names from `signal.h`'s, flag
families over the full umbrella — then probed on both sides. A name the
Cygwin side lacks is recorded as `-`, a finding rather than a failure.
A probe that does not fold to an integer constant (glibc's `SIGRTMIN`
is a function call) prunes itself out through the compiler's error
lines and lands in `dropped.tsv` with the reason, so the exclusion
list maintains itself. Constants the Cygwin side defines as enums
rather than macros would also read as absent; none of the probed
families are enum-defined in newlib or winsup today.

## The tables

    table           rows  contents
    errno-map.tsv    133  errno name, Linux value, Cygwin value
    signal-map.tsv    37  signal name, Linux value, Cygwin value
    flags.tsv        477  O_*, F_*, FD_*, AT_*, MAP_*, PROT_*, MADV_*,
                          MCL_*, MS_*, SOCK_*, SO_*, SOL_*, AF_*, PF_*,
                          MSG_*, SHUT_*, IPPROTO_*, POLL*, SEEK_*, S_I*,
                          RLIMIT_*, RLIM_*, CLOCK_*, and the wait flags
    layouts.tsv      164  sizeof and per-member offset/size, both sides,
                          for the structs that cross the boundary: stat,
                          dirent, termios, the sockaddr family, rlimit,
                          rusage, sigaction, sigset_t, stack_t, flock,
                          msghdr/cmsghdr/iovec, pollfd, the time pair,
                          itimerval, linger, utsname, statvfs
    dropped.tsv        2  what the extraction excluded, and why

The tables answer quickly what nobody should trust memory for:
`EADDRINUSE` is 98 upstairs and 112 downstairs, `SIGUSR1` is 10 and 30,
`O_CREAT` is 64 and 512, `AF_INET6` is 10 and 23, `struct stat` is 144
bytes upstairs and 128 down with `st_size` at 48 versus 40, and
Cygwin's `dirent` leads with a private `__d_version` word where Linux
puts `d_ino`.

## Consumers (the WP-56 shim set)

Every table exists to feed a named consumer:

- `errno-map.tsv` — the errno translation every WP-56 down-call wrapper
  performs on the return path; the single most shared piece of the shim
  set.
- `signal-map.tsv` — the signal slice: `kill`, `sigaction`,
  `sigprocmask` and the WP-43/DR-0030 delivery path, which must speak
  Linux numbers upstairs and Cygwin numbers downstairs.
- `flags.tsv` — the fcntl/open slice (`O_*`, `F_*`, `FD_*`, `AT_*`,
  `SEEK_*`, `S_I*`), the memory slice (`MAP_*`, `PROT_*`, `MADV_*`,
  `MCL_*`, `MS_*`), the socket slice (`SOCK_*`, `SO_*`, `SOL_*`,
  `AF_*`, `PF_*`, `MSG_*`, `SHUT_*`, `IPPROTO_*`), poll, rlimit,
  clock, and wait slices, each translating its own families inbound.
- `layouts.tsv` — the struct-rewriting shims: `stat`/`fstat`/`lstat`,
  `readdir`, termios ioctls, the socket address functions,
  `getrlimit`/`setrlimit`, `sigaction`, `recvmsg`/`sendmsg`,
  `uname`, `statvfs`. A row whose two sides match is how a shim earns
  the right to pass a pointer through unchanged; a row that differs is
  the copy loop it must write.
- `dropped.tsv` — consumed by review rather than code: it is the list
  of what the mechanical extraction refuses to claim.

## The net under this package

The extraction sees names, values, offsets and sizes. A field with the
same name, offset and size whose *meaning* differs is invisible here,
and the plan says so: WP-56's differential against a real el8 userland
is the net under this package, not the package itself.

## Reproducing

    t/reproduce.sh

fetches the pinned kernel headers if absent, reruns the extraction into
a scratch directory, and requires byte-identity with the committed
tables, then spot-checks one known divergence per class so a table of
accidental zeros cannot pass. Byte-identity is asserted for this tree's
toolchain (the primary root's gcc 14 — DR-0038); the values themselves
are toolchain-independent facts of the two header trees.
