# DR-0099 — `vfork` and `CLONE_VM | CLONE_VFORK` share the parent's memory under H and are a fork under N

Status: accepted
Date: 2026-09-05
Deciding: the operator, through the grant that accepted proposal 0012
Proposal: 0012
Amends: doc/design/Architecture.md § Process shape

## What was decided

The `vfork` syscall (58) and `clone` with `CLONE_VM | CLONE_VFORK` are
supported, and their semantics differ by substrate.

Under H the child is a task on the parent's page-table root; the parent's
tasks are held until the child calls `execve`, which gives it a root of its
own, or exits. That is Linux's contract exactly, including the child's writes
being visible to the parent.

Under N there is one NT process per Linux process and no way to run a child on
the parent's memory, so both are a fork followed by a wait on the child's
`exec` or exit. The `sysdeps` port for N patches two files 0011 § 16 did not
list, `sysdeps/unix/sysv/linux/spawni.c` (the pipe-based error report glibc
used before 2.24, in place of the write into `args->err`) and
`sysdeps/unix/sysv/linux/x86_64/vfork.S` (a fork). A program relying on
`vfork`'s shared memory in its own code sees a recorded divergence under N.

`clone` with `CLONE_VM` and neither `CLONE_VFORK` nor `CLONE_THREAD` remains
`EINVAL` under both, as 0011 § 6 had it.

## Why

0011 § 6 stated that glibc's `posix_spawn` uses `CLONE_VFORK` without
`CLONE_VM`. Spike 46 read the shipped `libc-2.28.so`: `posix_spawn` and
`posix_spawnp` reach `__spawnix`, which calls `__clone` with `0x4111`,
`CLONE_VM | CLONE_VFORK | SIGCHLD`, and `__spawni_child` reports an `exec`
failure by writing into the parent's stack; `__vfork` is syscall 58. As 0011
had it, `posix_spawn` under H would receive `EINVAL`, and a fork-plus-wait
would return success for a child whose `exec` failed. Tier 1 on the ladder:
under H one candidate is correct and it costs nothing 0012 § 2 did not build;
under N the shared address space does not exist, and tier 5 chose the pipe
report over a silent divergence.

## Consequences

`Architecture.md` § Process shape carries the contract per substrate and cites
this record. 0011 § 16's file list for N's `sysdeps` port grows by two. The
core's `clone` dispatch accepts `CLONE_VM | CLONE_VFORK` and syscall 58 under
both substrates and hands them to the seam's realisation.
