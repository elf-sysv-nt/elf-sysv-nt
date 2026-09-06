# Substrate H against WSL2

*A note for the client that permits the hypervisor. Written 2026-09-06 at
the operator's request (`hypervisor-prerequisite`, 2026-09-05). Not a
governed document; it argues, and the records it cites decide.*

Where the hypervisor is forbidden, WSL2 is forbidden with it and only
substrate N is on offer; spike 40's policy probe says so and 0012 § 2 makes
it the reason two substrates exist. Where the hypervisor is permitted, WSL2
is usually already installed, and a buyer asking for substrate H is asking
why they should want a second way to run Rocky Linux 8 on a machine that
has one. This page is the answer, and it is not that H is faster.

## What the two things are

WSL2 is a Linux virtual machine: Microsoft's kernel, a distribution's
userland, a VM boundary between them and Windows, and bridges across that
boundary for files (9P or virtiofs), the network (NAT or a mirrored stack),
and process launch (`wsl.exe`). Everything Linux runs inside at native
speed; everything shared with Windows crosses a wire.

Substrate H is the same el8 userland on a kernel this project writes,
running as one Windows process per user (0012 § 2). The hypervisor is used
for one thing, the page tables that give each Linux process its own
address space and let a shipped binary's `syscall` instruction and
`%fs`-relative loads run unmodified (0011 § 4.3). There is no second
kernel, no VM, no wire: a Linux process is threads in an NT process, a file
is an NTFS file, a pipe is an NT pipe, a socket is a Winsock socket, and
the user is the Windows user.

## Where H is the better answer

**One machine, not two.** Under WSL2 the Linux side is a separate computer
that shares a disk badly. `ls` on `/mnt/c` walks 9P; a build tree kept on
the Windows side runs at a fraction of native speed, and a tree kept inside
the VM is invisible to Explorer, to the backup agent, to the EDR, to the
Windows editor, except through the same bridge. Under H there is one tree.
A file the Linux process writes is the file Windows sees, with the LX
metadata WSL itself defined so both read it identically (0011 § 8, spike
39), and it was written by the Windows user with the Windows ACL, which is
what the auditor asks about.

**One process model.** A Linux process under H shows up in Task Manager,
in `Get-Process`, in the EDR's telemetry, in the job object the session
put it in, with the Windows user's token. Under WSL2 it is a process in a
VM the Windows tools cannot see into; the enterprise answer to that has
been to instrument the VM separately or to forbid it. Signals, pipes,
consoles and exit codes cross between a Windows program and a Linux
program under H the way they cross between two Windows programs (0011's
goal statement); under WSL2 they cross through `wsl.exe` and `interop`.

**One security principal.** Nothing under H runs as anything but the user
who started it; there is no VM root, no `sudo` that means something
different from the Windows account, no second set of credentials to
manage. That is 0011's stated non-goal (no security boundary) turned
around: for a client that already trusts the user, the absence of a
boundary is the absence of a second machine to secure.

**Administration.** WSL2 is a kernel Microsoft updates, a distribution the
user updates, a `.vhdx` that grows and does not shrink, a memory balloon,
and a service. H is a user-mode program with a version number, installed
per user, whose entire state is a tree under one installation root (0011
§§ 2 and 8), and
whose kernel behaviour is the same on every machine because it is the
project's code rather than whichever kernel the host's WSL is at.

**Fidelity where it is checked.** WSL2 runs any Linux program because it
runs a Linux kernel; that is its strength and cannot be argued with. H
runs the programs the twelve criteria name (Verification-Plan § The
kernel's criteria) and whatever else the syscall table it implements
covers, and every one of those behaviours is measured against a Rocky 8
oracle rather than assumed. The client who needs an arbitrary program is
better served by WSL2; the client who needs these programs, and needs
them to behave as NT citizens, is the one this note is for.

## Where WSL2 is the better answer

Breadth: anything that needs a kernel facility H has not built (network
namespaces, cgroups, the container stack above them, kernel modules, a
real `systemd`) runs under WSL2 and not here; 0011 lists those as
non-goals. Raw compute in a tight syscall loop: a syscall under H is a
hypervisor exit at about 5 µs (spike 40), against tens of nanoseconds
inside a VM, and a `fork` is a page-table copy at half a millisecond plus
25 µs per copy-on-write fault (spike 44), against Linux's own. A workload
that is syscalls and forks all day is slower under H, by a factor that
matters for a shell script and not for a compiler. And isolation: a client
who wants the Linux side confined wants the VM.

## The comparison a buyer makes

Not "which is faster" but "how many machines am I running, and who can
see into them". H's case is that a Linux program on a Windows desktop
should be a Windows program that happens to speak Linux: one filesystem,
one process list, one user, one thing to audit. WSL2's case is that a
Linux program should run on Linux. Both are right; they are answers to
different questions, and the one the managed desktop tends to ask is the
first.

*Cites: 0011 §§ 2, 4.3, 8 and its goals; 0012 § 2; spikes 39, 40, 42–45;
Verification-Plan § The kernel's criteria.*
