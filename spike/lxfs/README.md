# lxfs

Does this NTFS volume, reached through NT's own interfaces, already carry the
Linux metadata WSL's DrvFs uses? If it does, the kernel's on-disk format can be
WSL's rather than an invention of its own. `results-2026-09-04.txt` is the
transcript; the reading is below it.

**Gates.** Proposal 0011's Phase 0, spike (e), and through it the whole of § 8.

## Why it matters

Proposal 0011 § 8 does not design a metadata format. It borrows one: `$LXUID`,
`$LXGID`, `$LXMOD` and `$LXDEV` as NTFS extended attributes,
`IO_REPARSE_TAG_LX_SYMLINK` for symlinks, the `LX_CHR`/`LX_BLK`/`LX_FIFO` tags
for special files, `FILE_CASE_SENSITIVE_INFORMATION` per directory,
`FILE_DISPOSITION_POSIX_SEMANTICS` for `unlink`. The argument for borrowing is
interoperation, and the proposal writes that argument down as verification
criterion 3, which asks that a tree written by the kernel be read by WSL and by
Cygwin 3.6 "with identical modes, owners, symlink targets and special files."

Against that stands the proposal's own "Not verified" list, which names one of
these behaviours outright: that `FileStatLxInformation` returns the four LX
fields through `NtQueryInformationByName`. That single class carries the
design's whole `stat` story, since it is what lets a path be stat'd without
being opened, which is the difference the proposal claims over Cygwin. So the
question here is not academic. If the class is absent, or present but blind to
the EAs, § 8 loses both its format and its performance argument in one go, and
the borrowed-format decision has to be reopened before anything is built on it.

## Running it

    bash measure.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is asked for, which is itself part of the
measurement: setting a directory case-sensitive is normally an administrator's
operation, and whether it is one here was a thing to find out rather than to
work around. The script builds a native mingw probe and a small Cygwin
comparison program, makes a scratch tree under `$TMPDIR` on the NTFS volume,
runs both, has WSL and Cygwin read the tree back, and removes everything. A
full run takes about half a minute, most of it in the 2000-file `stat` tree.

`--count N` shrinks that tree for a quick pass. `--keep` leaves the two
binaries beside the sources. Cleanup goes through the probe's own `--clean`
rather than `rm -rf`, because two of the specimens are `LX_CHR` reparse points,
and Cygwin, which reads them as character devices, refuses to remove them.

## The eight questions

`lxfs-probe.c` asks six of them against NT directly, with `lxfs-nt.c` holding
everything that reaches into `ntdll`. The three information classes at the
centre of the spike live in `ntifs.h`, a driver header mingw does not ship, so
`lxfs-nt.h` writes out the structures by hand; a wrong layout comes back as
`STATUS_INFO_LENGTH_MISMATCH` rather than as a plausible number read from the
wrong offset. `IO_REPARSE_TAG_LX_SYMLINK` is the one constant checked against
real source, Cygwin's `winsup/cygwin/path.cc:1954`, and the reparse buffer
below it is Cygwin's `REPARSE_LX_SYMLINK_BUFFER` shape including the `FileType`
of 2 that its own comment says to take with a grain of salt.

The remaining two questions are the harness's. `measure.sh` hands the same
reader script to a WSL distro and to Cygwin, so a difference between them is a
difference in what they see rather than in what they were asked; and it runs
`stat-rate.c`, built by Cygwin's gcc against Cygwin's libc, over the tree the
probe timed with `NtQueryInformationByName`.

## The verdict, 2026-09-04

`finding=lxfs-complete-cygwin-blind`. Every NT interface § 8 names works on
this volume, from an unelevated process, and WSL reads back everything the
kernel would write. Cygwin does not.

**The stat class is there, and both paths work.** Class 70 filled a 96-byte
structure on an open handle and again through `NtQueryInformationByName` with
no file opened, and the two agreed field for field, file id included. `LxFlags`
carried the `HAS_UID`, `HAS_GID` and `HAS_MODE` bits, so the fields are
asserted present rather than merely zero. The proposal's unverified claim is
now verified, in both of its forms.

**The EAs round-trip, and the stat class reports them.** Those are two
findings, and they are recorded apart on purpose, since a volume could store
the attributes as inert bytes and leave the fast `stat` path saying nothing
about them. It does not: a mode of `0100751` written through `NtSetEaFile` came
back through `NtQueryEaFile` and showed up in `LxMode`. `$LXDEV` needs its
reparse tag beside it. The device-id bit in `LxFlags` appeared only once the
file also carried `IO_REPARSE_TAG_LX_CHR`, which is how DrvFs writes a device
node and not a detail the EA documentation makes obvious.

**Neither the symlink nor the case-sensitive directory wanted a privilege.**
The token did not hold `SeCreateSymbolicLinkPrivilege` and the process was not
elevated. `FSCTL_SET_REPARSE_POINT` took the LX symlink tag anyway and the
UTF-8 target survived; `FileCaseSensitiveInformation` took as well, `Makefile`
and `makefile` were both created through `NtCreateFile` without
`OBJ_CASE_INSENSITIVE`, and a directory enumeration found both names standing.
The stat class then reported the directory's own `LX_FILE_CASE_SENSITIVE_DIR`
bit, which makes it the cheap way to ask a directory what it is.

**POSIX delete works, with a constraint the design has to respect.** Setting
`FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS` returned success,
but the name did not leave the directory at that moment: a lookup still found
it and failed with `ERROR_ACCESS_DENIED`, the classic delete-pending answer.
The name left when the handle carrying the disposition was closed. Afterwards
the file was gone by name, a new file took the name immediately, and the
separate descriptor held open across the whole sequence still wrote and read
its own now-nameless file. That is Linux `unlink`, and it is reached by
opening a handle for `DELETE`, setting the disposition, and closing that handle
— the shape Cygwin already uses at `syscalls.cc:768`. A VFS that set the
disposition on the descriptor it was keeping open would find the name still
occupied and would race with itself.

**WSL agrees completely; Cygwin reads the symlink and stops there.** The rocky8
distro reported `644 1000 1000` and `755 1234 5678` for the two regular files,
resolved the symlink to its target, and listed the device node as a character
special file with major 1 and minor 3, all of which is what was written.
Cygwin 3.6.10 resolved the symlink, and nothing else: both files read as mode
755 owned by the Windows account, and the device node read as a regular empty
file. So criterion 3, as it is written, cannot pass on this host. That is a
finding about Cygwin's `stat`, which derives a mode from ACLs and does not
consult the LX attributes, rather than about the format or the volume, and the
criterion needs rewording — or the Cygwin side of it needs a mount option and a
measurement of its own — before it is treated as a gate.

**The rate, for scale and nothing more.** Over 2000 warm files,
`NtQueryInformationByName` with class 70 cost about 10 µs a file and Cygwin's
`stat()` about 86 µs, roughly nine to one. Those numbers move every run and no
part of the verdict turns on them.

## What this does not reach

Only one direction of criterion 3. A tree written by the kernel is read by WSL
here; a tree written by WSL and read back through these interfaces is not, and
the criterion asks for both.

Only one WSL. The distro is a WSL2 one reaching `/mnt/c` through its plan9
server, and the proposal's text says WSL1's DrvFs. The two are documented to
share the format, and this spike did not test that they do.

Only one volume, the operator's local NTFS `C:`. Nothing here says what a
network share, a ReFS volume, a mounted VHD or a FAT-formatted stick does, and
§ 8's fallback paths for file systems without POSIX semantics are exactly the
case that is unmeasured.

Several pieces of § 8 are simply not in this probe: `FILE_RENAME_POSIX_SEMANTICS`,
hard links, the `U+F000`-to-`U+F0FF` escape for the nine characters NTFS
refuses, the `AF_UNIX`, `LX_FIFO` and `LX_BLK` tags, `getdents64`, and the path
resolution that splices a target in on `STATUS_IO_REPARSE_TAG_NOT_HANDLED`.
Only `LX_CHR` stood in for the special-file family.

The rate is a micro-benchmark and should not be read as a `git status`
prediction. It is one thread walking names it generated itself, on a warm
cache, in a single flat directory, with an anti-malware filter in the stack
whose contribution nobody separated out.

Whether the case-sensitivity flag can be set by someone other than the file's
creator. It was set here by the account that made the directory, which is the
easy case, and the installer § 8 describes may not be running as that account.
