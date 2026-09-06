# Roadmap

The order of work, and where to read the position. This is a scope document
rather than a status one: nothing here records what is finished, because
scope and progress rot at different rates, and a file that carries both goes
stale at the faster of the two.

## The order

Proposal 0011 § 18 is the order of work, and this page does not restate it.
Phase 0's measurements came first and are the spikes `doc/milestones.md`
records; proposal 0012's own phase 0 (the process seam, H's shape, the
`vfork` contract) sits between 0011's phase 1 and phase 3, and its § 10 says
what each later phase gains from the seam. In one line, for a reader who
has not opened 0011:

    substrate -> a process (1) -> files (2) -> the ported glibc and the
    supervisor (3) -> threads (4) -> terminals (5) -> readiness and
    sockets (6) -> IPC and locks (7) -> tracing (8) -> the LTP run and the
    package acceptance (9)

Phases 1 and 2 are one person's path. Phase 3 splits into the toolchain
half (substrate N's glibc port and the post-link check) and the supervisor
half (the seam's cross-process realisation), which are independent. Phases
5, 6 and 7 are independent of each other once 3 and 4 stand. Substrate H's
leaf is an independent unit from the moment the seam's header exists, and
replaces the gate and the arena in phase 1 and the toolchain half of phase 3
while touching nothing from phase 4 on.

## Where the position is read

`test/suites.tsv` names every suite and its tier, and a criterion joins it
when it is met; `doc/design/Verification-Plan.md` § The kernel's criteria
says which are met. `doc/design/Core-Phase1.md` and `Core-Phase2.md` record
the phases that are built, with their decision logs and the divergences
they chose. `doc/milestones.md` holds every spike and its verdict.
`doc/deferred-work.md` holds what was set aside and why.

## The decisions this page once tracked

The veneer-era roadmap carried a table of three decisions and a branch for
each. All three are settled and the table is gone: the target triple by
DR-0001; the boundary itself, a kernel at the syscall boundary rather than
a re-faced C library, by 0011 and DR-0097; and the thread pointer under
substrate N by DR-0101, with substrate H using `%fs` as the ABI says. That
roadmap is kept whole as `doc/history/veneer-roadmap.md`, as reasoning
that was true of a design the project no longer builds.
