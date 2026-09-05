# What WHvMapGpaRange does to the memory behind it

Does mapping host memory into a partition populate it, pin it, or leave it to
the memory manager? What does the guest see when the host page behind a
mapped guest-physical address is reserved but uncommitted, decommitted, or
released? And what does a map call cost?

The answers, on this host: mapping is lazy and pins nothing; a reserved page,
a decommitted page and a released page all reach the host as a memory-access
exit that says the GPA is mapped and the host page absent; committing behind
the exit and resuming works. `results-2026-09-05.txt` is the transcript.

## Why it matters

Substrate H's memory design in 0011 § 4 is one sentence: "the host memory
behind guest physical pages is still NT memory in the host process, so the
lazy-commit handler still exists, one level down." Nothing had measured
whether that sentence is true. If `WHvMapGpaRange` pinned what it mapped, a
`MAP_NORESERVE` reservation under H would be resident RAM, and the
single-kernel-process shape (shape B, one kernel process for every Linux process) would pin
every Linux process's touched memory into one working set. That note names
this measurement first among the ones that decide between H's shapes.

**Gates.** The H memory story in either shape, and through it the topology
choice proposal 0012 has to make.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Unelevated, nothing installed; a run is under a second. Two of the questions
(q4 and q5) do things that could kill the process that asks them, so the
probe runs them in a child of itself and reports how the child ended
(`exited-clean`, `died:0x...`, or `hung`); a crash there is a finding, not a
broken spike.

## Method

`gpa-probe.c` builds a ring-0 guest by hand, as `spike/whp/` does: four
megabytes of control memory at GPA 0 holding identity page tables over the
first four gigabytes in 2 MB pages, a GDT, a TSS, and two six-byte loops,
`mov (%rbx),%rax; hlt; jmp` and its store twin. The host aims `%rbx`, runs,
and reads `%rax` back; one `WHvRunVirtualProcessor` call is one touch.

q2 commits a gigabyte of host memory without touching it, maps it, and asks
the memory manager rather than the API what happened: the working set before
and after, and `QueryWorkingSetEx` over 256 pages spaced through the range
for the `Valid` and `Locked` bits. The hypervisor's own count of mapped 4K
pages comes from `WHvGetPartitionCounters` for contrast: it reports the
gigabyte mapped while the memory manager reports none of it resident.

q3 has the guest read each of those pages once and again, timing both, then
re-samples residency. q4 maps a reserve-only gigabyte, touches it from the
guest, reads the exit's `GpaUnmapped` bit, commits the page behind it,
resumes, and then times that sequence over 256 pages: the lazy-commit path
under H, to set beside spike 37's q7 under N. q5 writes a pattern through the
host, reads it from the guest, decommits the page, touches again, recommits,
touches again, releases the whole allocation while mapped, and touches once
more. q6 times 2048 4 KB maps and unmaps and 32 2 MB ones, then
`WHvAdviseGpaRange` with populate and pin advice over 64 MB, with a guest
first touch afterwards to see whether populate bought the demand fault back.
q7 leaves a range unmapped, lets the guest fault into it, maps the page on
the exit, resumes, and checks the value; 1024 times.

The `Is*Present` helpers the header declares live in an API-set stub the
import library does not carry; the probe asks `WinHvPlatform.dll` for the
export by name instead, which answers the same question.

## What the transcript says, read for the design

Mapping is a SLAT edit, not a population. The hypervisor records the
gigabyte mapped and the host has committed nothing more than before; the
memory manager backs a page the first time the guest touches it, at about
13 µs against 4 µs for a page already resident, and leaves it unlocked. A
populate advice over 64 MB costs about 6 ms, under 0.4 µs per page, and a
first touch inside a populated range costs what a resident one does. That is
the lever: populate in chunks around a fault rather than take the demand
fault per page.

The host owns the pages. Decommit and release behind a live mapping both
succeed, and the guest's next touch is a memory-access exit distinguishable
from an unmapped GPA (`GpaUnmapped` clear, "mapped, host absent"). So
`MADV_DONTNEED` is a decommit, a reserved range can sit behind a mapping
until first touch, and the kernel can tell "no VMA" from "VMA, no page" at the
exit without a lookup.

A 4 KB map call is about 7 µs and a 2 MB one about 30 µs, so mapping page by
page on exits (q7, about 25 µs per fault) is the slow way and pre-mapping
reserved memory then committing on the exit (q4, about 32 µs) is no faster;
the fast way is to map and populate in large ranges and let the guest touch
resident pages at 4 µs.

## What this does not reach

One partition, one vCPU, one gigabyte. Whether the lazy behaviour holds at
tens of gigabytes mapped, or under memory pressure that pages a mapped page
out and back, is not measured. The `Locked` bit `QueryWorkingSetEx` reports
is the memory manager's notion of a lock (`VirtualLock`), and the pin advice
did not set it; whether the hypervisor pins by some other accounting is not
something this probe can see. The numbers are one host, one Windows build,
one AMD part.
