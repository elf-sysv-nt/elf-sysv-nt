# Core Phase 2: files

The VFS, the descriptor layer, and the host file store beneath them, on
substrate N, checked against real Rocky 8 and against WSL. This is proposal
0011 § 18 Phase 2, verification criteria 2 and 3 (criterion 3 as DR-0102
amended it), built under the operator's grant of 2026-09-05. `Core-Phase1.md`
is the process this rests on; `doc/design/Architecture.md` § The shape of the
system draws the lines it stays inside.

## The increment

Criterion 2: a scripted sequence of file operations produces the same trace
of results and errnos here as on the el8 reference. Criterion 3: a tree
written by the kernel is read by WSL with identical modes, owners, symlink
targets and special files, and the reverse holds for a tree written by WSL.
The pieces, leaf first, each certified before the next leaned on it:

1. **The host file store** (`core/hostfs.h`, `core/hostfs_nt.c`). The line
   between the VFS and NTFS: handles and single components in, Linux modes,
   uids and nanosecond times out, negative Linux errnos back. The NT side
   keeps the LX attributes and reparse tags WSL defined, uses POSIX delete
   and rename, escapes the characters NTFS refuses in a name the way WSL
   does, and reads Windows symbolic links as symlinks. It is the one file
   under `core/` that names NT calls, and it says so in its head with
   `substrate-line: below`, which `bin/check-substrate-line` reads and walks
   past. `core/t/hostfs-test.sh` is its bar: 104 checks on this volume.
2. **Descriptions and descriptors** (`core/file.h`, `core/file.c`). 0011
   § 9's shape: a description with a small set of operations every kind
   implements, a per-process table with lowest-free allocation, `dup`
   sharing the description and the offset, `O_CLOEXEC` on the descriptor.
3. **The VFS** (`core/vfs.h`, `core/vfs.c`). The mount table, the walk
   (lexical `..` over a canonical path, LX and Windows symlinks spliced in,
   `ELOOP` at 40, a mount point changing file systems), Linux's
   discretionary permission check, and the host-backed file and directory
   kinds. Every host access is an open of one component under a directory
   handle the walk holds.
4. **The in-kernel kinds** (`core/vfs_synth.c`): `/proc/self` (`maps` from
   the VMA tree, `exe`, `cwd`, `fd/`), `/dev` (the memory devices, random,
   the standard-stream links), the pipe as a ring in kernel memory, and the
   console the process inherited as the foreign handle of 0011 § 9.
5. **The syscalls** (`core/sys_fs.c`): every file syscall Phase 2 names and
   the `*at` family with them, 60 numbers, each copying across the boundary
   through the substrate's `user_copy_*` and a kernel bounce buffer (0012
   § 4), so no user address reaches a host call under either substrate.
6. **The programs and the harnesses.** `test/core/lksys.h` and `lkcrt.S`
   let a freestanding C program be built once against the gate and once,
   with `-DLK_ORACLE`, against the `syscall` instruction. `vfs-trace.c` is
   criterion 2's sequence, 291 observations; `test/t/vfs-diff.sh` runs it
   under `lk-host` over a fresh root and on the oracle in a fresh directory
   and diffs. `lxfs-tree.c` writes or reads criterion 3's tree;
   `test/t/lxfs-interop.sh` has the kernel and WSL each write one, reads
   both back with both, and diffs the manifests.

## Layout

`core/` gained the files above and `core/build.sh`, which holds the one
source list both harnesses build `lk-host` from; `core/t/` holds the host
store's bar. `test/core/` holds the freestanding programs and their runtime;
`test/t/` the criterion harnesses, registered in `test/suites.tsv` at the
report tier because each needs both toolchains and WSL. `lk-host` gained
`--root=DIR`, the host directory that is the Linux root, and `--exe=PATH`,
the name the program is known by. The ELF mapper now realises a segment as
one anonymous range filled through `user_copy_out`, because a file map and
a `.bss` map cannot share a granule under N; the user stack is 8 MB.

## Decision log

Each entry names the tier of the decision ladder that settled it.

- **D1 -- the walk re-opens the directory chain from the mount root for
  each resolution.** Tier 2 (economy) over 0011 § 8's fast path, which
  hands the whole path to one NT open and keeps a per-process handle
  cache. The component walk is what gives the VFS Linux's rules between
  components; the fast path is an optimisation over it, taken when a
  measurement asks for it and not before.
- **D2 -- `$LXMOD` carries the whole `st_mode`, type bits included.**
  Tier 1, found by criterion 3's harness: WSL reads a bare permission set
  as no metadata at all and shows `0000`. The store adds the type on
  creation; `chmod` writes type and permission together.
- **D3 -- a Windows symbolic link is a symlink to Linux.** Tier 1, found
  the same way: WSL with `metadata` writes a relative symlink as a Windows
  one, so a tree it wrote reads back wrong unless the kernel reads that
  tag. `readlink` gives the link's name with backslashes turned to slashes,
  and a drive-absolute target as `/mnt/<drive>/...`, WSL's spelling. A
  junction stays transparent, as 0011 § 8 says.
- **D4 -- the root tree and every directory the kernel creates under it
  are case-sensitive.** Tier 1, criterion 2: `stat A:B*C?D` must fail
  where `a:b*c?d` exists. `vfs_mount_host` marks the root and `mkdir`
  marks its children on an LX-capable volume; a drive mounted under `/mnt`
  keeps Windows's rule.
- **D5 -- a segment is mapped as one anonymous range and filled by copy.**
  Tier 1: substrate N's reservation is the granule, so a file-backed map
  for the file part and an anonymous map for the tail of the same segment
  cannot both be realised (spike 42's lazy mapping is H's story, not N's).
  DR-0061's granule-separable link keeps segments apart, not a segment's
  own halves. Under H the copy is a cost a real mapping would avoid; the
  mapper is the place that changes, and nothing above it knows.
- **D6 -- the pipe is a ring in kernel memory, and a read that would block
  returns `EAGAIN`.** Tier 5 (diagnosability). 0011 § 9's ring in a shared
  section needs a second process to matter, and phase 2 has one thread;
  a read that blocked would block forever. The divergence is stated at
  the file's head and goes with phase 3's fork.
- **D7 -- criterion 3 is measured through a DrvFs mount WSL makes with the
  `metadata` option.** Tier 1: the standing `/mnt/c` mount on this host
  has no `metadata`, so WSL's `chmod` and `chown` through it are silent
  no-ops and the reverse direction could not be measured. The harness
  mounts each tree itself; spike 39 measured the reading direction through
  the standing mount and agrees.
- **D8 -- Linux errno values are a header of their own.** Tier 1: mingw's
  `errno.h` numbers `ENAMETOOLONG`, `ENOSYS`, `ENOTEMPTY` and `ELOOP`
  differently, and a host header pulled in for an NT call brings it along.
  `core/lxerrno.h` undefines and redefines every name, and is included
  after any host header.

## Recorded divergences

Class C in `Requirements.md`'s sense, each observable and each chosen:

- NTFS keeps timestamps at 100 ns; a `utimensat` with a finer nanosecond
  count reads back truncated. Linux on ext4 keeps the nanosecond.
- `st_nlink` of a directory is what NTFS counts, 1, where Linux counts
  2 plus subdirectories. (The trace prints no directory link counts.)
- `/dev` is read-only: `mkdir /dev/x` is `EACCES` where Linux's devtmpfs
  allows root.
- A pipe read that would block returns `EAGAIN` until phase 3 (D6).
- Truncating a file below a live mapping is refused, and a directory
  cannot be renamed while a handle is open beneath it (spike 39 q9); the
  VFS reports the host's `EBUSY` in both cases.

## The result

Criterion 2 is met: the 291-line trace is identical under the core and on
the Rocky 8 oracle, exit codes equal, and `test/t/vfs-diff.sh` says so; it
was watched to fail on sixteen lines before the last of the fixes above,
each a difference the oracle found. Criterion 3 is met: the kernel's tree
reads back from WSL with 22 objects identical in type, mode, owner, target
and device numbers, WSL's tree reads back from the kernel the same, and the
two trees read the same whichever side wrote them; `test/t/lxfs-interop.sh`
says so, and was watched to fail on the type bits and the Windows symlink.
Criterion 1's remainder came with it: `/proc/self/maps` lists the program,
the stack and the vDSO from the VMA tree the mapper now records.

What this is not: a second process, a signal, a thread, a tty, or a file
opened by a real el8 binary; those are phases 3 to 5. The special files on
the host store are created and reported but not opened (`ENXIO`); the
FIFO's other end is phase 3's. `mount(2)` is absent, and the mount table is
the three mounts `vfs_init` seats.
