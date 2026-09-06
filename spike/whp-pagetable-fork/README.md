# fork as a page-table copy inside one partition

Can the host be the kernel for a guest whose page tables it writes: take the
guest's page fault as an exit, copy the page, resume the store? What does the
table copy cost for a 64 MB and a 576 MB process, what does one copy-on-write
fault cost end to end, is the isolation right in every direction, and what
does moving a vCPU between two roots cost?

Yes on every count. A `#PF` reaches the host as `WHvRunVpExitReasonException`
with the fault address and error code, `%rip` still at the store; copying the
tables of a 576 MB process takes about 0.5 to 0.75 ms; a copy-on-write fault
costs about 25 µs over the same store without a fault; parent, child and
grandchild each see their own bytes; and the stale read-only translation does
not have to be flushed. `results-2026-09-05.txt` is the transcript.

## Why it matters

Shape B of substrate H (one kernel process, a page-table root per Linux process) keeps every Linux process
in one partition as a page-table root and forks by copying the tables. 0011
§ 4 describes fork under H in exactly those words, "mark every writable entry
read-only in parent and child and copy on the first write fault", for a
kernel that lives outside the guest and has no IDT of its own. The review of
0011 found that nothing had run
it. This is the measurement, and it is the number to set beside spike 35's
5.1 ms `RtlCloneUserProcess`, which is what fork costs under N and under H's
other shape.

**Gates.** The topology choice in proposal 0012; criterion 4 (fork under
2 ms) for shape B.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Unelevated; under a second. `-n` sets how many copy-on-write faults q4 times
(1024). The probe commits about 660 MB of host memory for the guest's
physical memory.

## Method

`fork-probe.c` is a small memory manager for a guest. One host buffer is the
guest's physical memory at GPA 0; the first 4 MB hold a GDT, a TSS and two
six-byte loops (`mov %rax,(%rbx); hlt; jmp` and its load twin); a bump
allocator hands out 4 KB frames above that, with a reference count per frame.
A process is a four-level page-table root built by `map_range`, with control
memory identity-mapped and marked never-copy. The guest runs at ring 0 with
`CR0.WP` set, so a store through a read-only leaf faults as a user store
would, and there is no IDT: the partition is created with
`ExtendedVmExits.ExceptionExit` and bit 14 of `ExceptionExitBitmap`, so the
fault comes to the host instead.

`as_clone` walks the tree allocating and copying each table page; at the
leaves it clears the write bit in both the source and the copy, sets a
software copy-on-write mark, and bumps the frame's count. `cow_resolve`
walks the faulting root to the leaf, copies the frame if its count is above
one and otherwise takes it, and sets the write bit. The host's `run` loop
resolves exception exits until the guest halts.

q2 maps 64 MB and 512 MB of pattern-filled frames into a parent and has the
guest store and load without a fault. q3 times five clones of a 64 MB root
and five of the full parent, then releases four of each so the parent's
frames are shared exactly two ways. q4 has the child store to 1024 pages,
timing each store from the register write to the halt, and the host's own
work inside it; then checks every parent frame still holds the parent
pattern, every child frame holds the child's, and no two are the same frame,
first from the host and then through the guest under each root. q4b repeats
256 stores without the CR3 reload after the copy; q4c stores 256 times to
pages the child already owns, so the no-fault cost of the same call path is
known. q5 exercises the other paths: the parent storing to a page the child
never touched (copies), to one the child already copied (takes it), and a
clone of the child. q6 runs a faultless load 2000 times on one root and 2000
times alternating roots.

q7 (added 2026-09-06) repeats q4 from ring 3. The GDT gains DPL-3 code and
data descriptors, the two loops get twins that end in `ud2` instead of the
privileged `hlt`, the exception-exit bitmap takes `#UD` and `#GP` alongside
`#PF`, and the code page is given the user bit. A fresh clone of the parent
is entered with the DPL-3 selectors and stores to 256 pages it shares with
the parent; the transcript records the first fault's error code, its user
bit, the CPL at the exit and whether `%rip` is still at the store, then the
same isolation checks as q4. A ring-3 store to a supervisor page (the
identity-mapped control area, no user bit) is the control: it must fault and
never resolve.

## What the transcript says, read for the design

The exception exit is the mechanism the design needs. `ExceptionParameter`
carries the fault address (the CR2 register reads zero at the exit, so read
the parameter), the error code is `0x3`, and `%rip` has not moved, so the
host fixes the leaf and runs again. The re-executed store completed in one
fault in all 256 tries without any TLB flush: the hypervisor is not caching
the failed translation, and the CR3 reload the probe first did after each
copy was about 5 µs of waste.

The copy scales with the tables, not the memory: 37 table pages and about
0.1 ms for 64 MB, 294 pages and 0.5 to 0.75 ms for 576 MB, against the 5.1 ms
NT process clone spike 35 measured on a tiny process. That is what puts
criterion 4 in reach for shape B and not for shape A.

A copy-on-write fault costs about 37 µs from register write to halt, of
which 5 µs is the host walking and copying; the same store without a fault
costs 12 µs on that path (about half of that is the CR3 write the probe
includes on every call). The fault itself, then, is on the order of 25 µs,
which is ten to twenty times Linux's and about what a `WHvRunVirtualProcessor`
round trip through an exception exit costs. A child that touches a thousand
pages before `exec` pays 25 ms in faults; the design's answer is the one
Linux uses, copy eagerly on fork what the child will certainly touch, or
skip the fork with `vfork`, which shape B gives exactly.

Switching a vCPU between roots costs about 6 µs over running it on the same
root, the CR3 write and whatever the hypervisor does about it. Under shape B
with a vCPU per running thread that switch happens at thread migration, not
at every syscall.

From ring 3 the fault is the same fault with the user bit set: error code
`0x7` (present, write, user) at CPL 3 with `%rip` at the store, one fault
per page, every frame copied and isolated, and a round trip of about 24 µs,
the same as ring 0's. A ring-3 store to a page without the user bit is a
`#PF` with the same code that the resolver refuses, which is the privilege
check the kernel relies on to keep its own pages from the process.

## What this does not reach

The guest runs at ring 0 for q2 to q6 and at ring 3 for q7 only; a
signal frame, a `syscall` instruction and an IDT are not exercised, so
nothing here says how a ring-3 fault is delivered back into the guest, only
how it reaches the host. One vCPU, one thread. The pattern check reads one
word per page, not the page. Frames are never freed, since the probe has no
need. Table copy was measured for anonymous memory only; file-backed frames
would share without copy-on-write and cost less. One host, one Windows build,
one AMD part.
