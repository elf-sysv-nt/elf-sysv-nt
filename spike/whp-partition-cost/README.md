# What a partition costs, and how many one process may hold

How long from `WHvCreatePartition` to a vCPU's first exit; how many partitions
one process can hold with guest memory mapped; how many vCPUs one partition
carries; whether a vCPU can be run from one thread and then another; and
whether eight vCPUs exiting at once each pay what one pays.

The answer that matters most was not one of the questions as first asked: on
this host a process may set up several partitions but only one of them at a
time may hold guest-physical mappings. The second `WHvMapGpaRange` into a
second partition is refused with `0xC0370008` whichever partition mapped
first, and is accepted once the first unmaps. A second process holding its
own mapped partition is unaffected, and a section viewed in both processes
maps into both partitions and stays coherent. `results-2026-09-05.txt` is the
transcript.

## Why it matters

Substrate H has two shapes. Shape A holds a
partition per Linux process, one host process each, and builds one on every
fork; its cost floor is q2 here. Shape B holds every Linux process in one
kernel process, and had two sub-shapes: B1, a partition per Linux process in
that one process, and B2, one partition with a page-table root per process and
a pool of vCPUs handed between threads. q3 rules B1 out on this build. q4, q5
and q6 are what B2 needs: enough vCPUs, thread handoff, and exit cost that
does not collapse under concurrency.

**Gates.** The topology choice in proposal 0012, and shape A's fork budget.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Unelevated; a run is about a second. `-p` caps how many partitions q3 tries
to hold (512), `-c` how many vCPUs q4 creates (256), `-n` how many exits q6
times per vCPU (5000). Take q6 on an idle host; the per-vCPU medians spread
under load.

## Method

The guest is the ring-0 `inc %rax; hlt; jmp` loop from `spike/whp/`, over
identity 2 MB pages, so every vCPU in every partition runs the same six bytes
and each `WHvRunVirtualProcessor` call is one exit.

q1 creates, sizes and sets up a partition forty times, and deletes it forty
times, timing each. q2 goes the whole way forty times: create, set up, map
4 MB of control memory, create a vCPU, load its registers, run to the first
halt. q3 holds partitions with a 64 KB mapping and one vCPU each until a call
refuses, records the stage and status of the refusal, then starts a second
process that holds one of its own while this one still holds its count. q3b
sets up two partitions and maps into the second-created first, then the
first, then again after an unmap, to see whether the ceiling is on partitions
or on mappings and whether order matters. q3c creates a named section, maps a
view into this process's partition, writes through it, and has a child map
its own view into a partition of its own and report whether it sees the write.

q4 tries `ProcessorCount` from 1 to 2048 at setup, then creates vCPUs in one
partition until refused, and runs each one to a halt. q5 has two threads take
turns running one vCPU to its halt, a thousand rounds each, and reads the loop
counter back. q6 runs eight vCPUs on eight threads from one starting gun, each
timing its own exits, and reports the spread of their medians beside a
single-vCPU baseline in the same partition.

## What the transcript says, read for the design

A partition costs about 0.4 ms to create and set up and about 0.7 ms to the
first exit, with deletion at about 0.6 ms. Under shape A that is what every
fork adds to the 5.1 ms `RtlCloneUserProcess` spike 35 measured, before the
child remaps its memory.

One mapped partition per process. B1 is not available on this build: the
kernel process cannot hold a mapped partition per Linux process. B2 is, and
its three preconditions hold: `ProcessorCount` up to 2048 is accepted and 240
vCPUs were created and run (the 241st is refused with a VID status, which is
Hyper-V's per-VM ceiling); a vCPU runs from either of two threads in turn with
its state intact; and eight vCPUs exiting at once each see a median exit of
4 to 6 µs against 3.6 µs alone, for an aggregate above a million exits per
second on twelve host processors.

Shape A's `MAP_SHARED` works the way N's does: a section viewed in two
processes, each view mapped into that process's partition, is one memory.

## What this does not reach

Whether the one-mapped-partition rule is WHP policy or this build's; it was
not looked for in documentation and is reported as measured. vCPU creation was
capped at 256 by the probe; the 240 ceiling is Hyper-V's, not the cap. Exit
cost under concurrency was measured with eight vCPUs on twelve processors, not
oversubscribed. Handoff was measured between two threads over one vCPU, not a
pool of vCPUs over many threads. One host, one Windows build, one AMD part.
