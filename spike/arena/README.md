# The arena: one placeholder over the user range, replaced piecewise

Can a process reserve the whole user address range as a placeholder and then
swap pieces of it for section views, at a granularity fine enough to be useful,
committing the rest only when something touches it? `arena-probe.c` and
`arena-fault.c` take the measurement, `measure.sh` builds them and writes
`results-<date>.txt`, and the reading of that transcript is under **The verdict,
2026-09-04** below. It ran on 2026-09-04: yes, and at a finer granularity than
the design assumed.

**Gates.** Phase 1's VMA tree and arena, and through them every mapping call the
N substrate will ever make. This is spike (c) of the six that proposal 0011's
section 18 puts before any design commitment.

## Why it matters

Proposal 0011 section 4.4 hands the kernel's address space to NT and then has to
absorb two NT rules that Linux does not have. A reservation's base is aligned to
64 KB where a page is 4 KB, and a reservation is released whole rather than in
part. Its answer to both is one mechanism:

> at exec, the substrate reserves the entire user range it intends to hand out
> ... as placeholders in the sense `NtAllocateVirtualMemoryEx` gives the word. A
> placeholder can be split at 64 KB, then replaced by a section view or a
> committed range, without ever being released.

Everything downstream leans on that. `mmap` becomes a split and a replace,
`munmap` becomes a decommit that leaves the placeholder standing, `brk` grows by
committing into reserved space, and `MAP_NORESERVE` over 64 GB stays free of the
commit charge because a vectored handler commits the page that faults. If the
mechanism does not exist, section 4.4 has no address space and phase 1 has
nothing to build on.

The proposal knows it never checked. Its "Not verified" section lists, among the
NT behaviours it read out of documentation rather than measured, "that
placeholders accept a section view at 64 KB inside a reservation of tens of
terabytes". That sentence is this spike.

The granularity half is the part with teeth. If a placeholder can only be split
and replaced at 64 KB, then a `PT_LOAD` whose `p_vaddr` and `p_offset` differ by
something that is not a multiple of 64 KB cannot be a view of its file at all,
and the loader has to fall back to reading the segment into anonymous memory.
Section 4.4 accepts that cost on the strength of el8's linker laying shared
objects out at 2 MB congruence, which makes the fallback rare rather than
absent. Whether the fallback is needed at all is decided here.

## Method

One native probe, built with the mingw cross compiler rather than Cygwin's gcc,
because the question belongs to the NT memory manager and a Cygwin binary would
ask it through a layer with opinions of its own about `mmap`. Every call goes
through `ntdll` by name -- `NtAllocateVirtualMemoryEx`, `NtFreeVirtualMemory`,
`NtCreateSection`, `NtMapViewOfSectionEx` -- rather than through the kernel32
wrappers, so that a refusal comes back as an NTSTATUS worth recording instead of
a `GetLastError` code that has already thrown information away. Resolution is by
`GetProcAddress`, so a host without one of them says so rather than failing to
link.

Eight questions, each printed as `key=value` on stdout:

    q1  a placeholder over 1 GB, 64 GB, 1 TB, 8 TB, 64 TB and 127 TB, each
        reserved and released on its own, so a refusal is about the span asked
        for and not about what is still held
    q2  a piece split out of the middle of a standing placeholder
    q3  that piece replaced by a view of a pagefile-backed section, written and
        read back through
    q4  the same at 4 KB: a page-sized split, a page-aligned split, and a
        page-sized view replacing what the split produced
    q5  a view of a real file replacing a placeholder -- at 64 KB, at 4 KB, at a
        section offset aligned only to 4 KB, and with execute in the section's
        protection
    q6  a vectored handler over a plainly reserved span: touch, catch, commit
        the faulting page, continue
    q7  the cost of that round trip against a plain first touch on committed
        memory
    q8  after a replacement, whether the surrounding placeholder still splits
        and whether the views already placed were disturbed

Two answers are measurements and not findings, and the transcript says so where
it prints them: q7's two timings. They are there so that a later argument about
whether lazy commit is affordable has a number to start from, and they decide
nothing.

Three things in the probe exist to keep it from lying to itself. A split that
returns success has not necessarily split what was asked for, since NT is free
to round a request up to its allocation granularity and report no error, so
every split is followed by a `VirtualQuery` and the transcript records the width
that came out rather than the width that went in. Every view is written and read
back, because a mapping that succeeds and addresses nothing is a mapping that
would pass a status check. And the region states in q8 are reported as words --
`reserved-private`, `committed-mapped` -- rather than as an address dump, since
a live memory map moves every run and cannot carry a finding.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Usage follows docopt: `-o/--output` with `-` for stdout, `-p/--pages` to size
q7's sweep, `-k/--keep` to leave the built binary beside the sources when you
would rather step through it, and the usual `-q`, `-v`, `-V`, `-h`. Each is also
settable as `MEASURE_ARENA_<OPTION>`. Nothing is installed, no privilege is
wanted, and the scratch file q5 maps is created in the temp directory and
deleted when its handle closes.

## The verdict, 2026-09-04

Yes, and further than section 4.4 asked for. `results-2026-09-04.txt`, taken on
Windows 10.0.26200.9168 under Cygwin 3.6.10 with mingw gcc 14.4.0.
`finding=arena-holds-at-4k`.

The arena exists. A single placeholder reservation was accepted at every span up
to and including 64 TB, which is half the user address range, and the whole of
the rest of the spike was then conducted inside that 64 TB placeholder rather
than inside a convenient small one. The first refusal came at 127 TB with
`STATUS_NO_MEMORY`, which is where the ladder was aimed to end: 127 TB is all but
a sliver of what NT hands out on x64, and the sliver is `ntdll`, the PEB and the
TEBs, which are there before the process runs a line of its own code. So the
literal claim in section 4.4 -- the *entire* user range, one reservation -- is
refused, and the practical claim underneath it is not. A kernel that wants a
contiguous arena takes 64 TB and asks NT where the occupied pieces are, or takes
several placeholders; it does not get to name 128 TB and be handed it.

Splitting works, and a split piece takes a view. Sixty-four kilobytes came out
of the middle of the 64 TB placeholder with `MEM_PRESERVE_PLACEHOLDER`, the
piece measured exactly one granule wide afterward, and a pagefile-backed section
replaced it with `MEM_REPLACE_PLACEHOLDER`. Writes at the first and last
quadword of the view read back correctly. That is the sentence proposal 0011
could not defend, and it defends.

The granularity answer is the surprise, and it is the one worth carrying
forward. Every part of the 4 KB case that was expected to be refused was
accepted. A placeholder split at a page-sized request produced a region exactly
one page wide, not a granule -- checked by query, not assumed from the status. A
split at a base that was 4 KB aligned and nothing more was accepted the same
way. A page-sized view replaced that page-sized placeholder and read and wrote
correctly, and the remainder of the granule around it was still a placeholder
afterward. The file-backed case answers the same: a 4 KB view of a real file
replaced a 4 KB placeholder, its contents matched the file, and -- this is the
one that matters for ELF -- so did a 4 KB view taken at a *section offset* of
4096, which is a file offset with no 64 KB congruence to the address at all.

That result contradicts what section 4.4 assumes, in the direction that makes
the design simpler. The 64 KB congruence rule for `PT_LOAD` segments, the
`MAP_PRIVATE` read-into-anonymous fallback, the `EINVAL` for a `MAP_SHARED`
mapping that fails the rule, and the argument from el8's 2 MB layout that makes
the fallback rare: on this Windows build none of them are forced by the
placeholder mechanism. It is a measurement of one kernel and it should be read as
one -- 64 KB is what Microsoft documents, 4 KB is what this build does, and the
difference between those two is exactly the kind of thing a servicing update is
free to move. The prudent reading is that the arena is *correct* at 64 KB and
*fast* at 4 KB: build the loader so that it works when only granule replacement
is available, and let the page-granular path be an optimisation the kernel probes
for rather than an invariant it assumes.

File-backed views replace placeholders, including with execute. A section
created over a real file replaced a 64 KB placeholder, and the 256 KB of pattern
written to the file before mapping came back byte for byte through the view. A
second section over the same file with `PAGE_EXECUTE_READ` in its protection
replaced another placeholder and was accepted, which is the shape section 4.4
wants for an executable `PT_LOAD`: a view of the file on disk, opened for
execute, rather than anonymous memory turned executable afterward. The endpoint
argument in section 4.4 survives its first contact with the mechanism.

Lazy commit works exactly as described. A vectored handler over a plainly
reserved span caught the access violation on an uncommitted page, committed that
page with `NtAllocateVirtualMemory`, and returned `EXCEPTION_CONTINUE_EXECUTION`;
the faulting store re-executed and the value read back. Two thousand and
forty-eight further pages were driven through the same path one at a time, and
every one of them faulted, committed and resumed.

The cost is context and not a finding, and it is not small: the median
fault-and-commit round trip measured about four microseconds, against a plain
first touch on already-committed memory at tens of nanoseconds. The control is
low because NT's commit of a private region does most of the backing work up
front, so the comparison is not fault against fault; it is the whole
user-mode round trip against nearly nothing. Section 4.4 already anticipates the
shape of this by committing small mappings eagerly "because a fault costs more
than a commit", and four microseconds a page says that threshold wants to be
generous. A 64 GB `MAP_NORESERVE` that is then touched densely would spend
minutes in the handler. Nothing here says lazy commit is the wrong mechanism for
the case it exists for, which is a large mapping that stays mostly untouched; it
does say that the eager threshold is a real tuning problem rather than a detail.

And the arena survives being used. After a view was placed, the placeholder
either side of it read back as reserved, a second 64 KB piece split cleanly out
of the neighbouring placeholder and took a view of its own, and the first view's
contents were still intact afterward. Replacing a piece of the arena does not
cost the rest of it.

## What this does not reach

Written now rather than after the fact, so the limits are not mistaken for
results.

The 4 KB result on another Windows build. This is one kernel on one machine, and
page-granular placeholder splitting is not what Microsoft documents. The spike is
kept so the question is cheap to ask again, and the design should not be built so
that a return to 64 KB granularity is a rewrite.

What happens to the arena across `fork`. Spike (a) asks whether
`NtCreateProcessEx` clones an address space at all; whether a cloned address
space arrives with its placeholders still placeholders, its views still views,
and its split geometry intact is a different question and neither spike asks it.
That gap sits directly under section 5's `fork`.

Anything about a full arena under pressure. The probe places a handful of views
in a 64 TB placeholder. It does not place ten thousand, does not measure what a
VAD tree of that size costs to walk, and does not find where NT starts refusing
splits. A `mmap`-heavy process is the shape that would find that ceiling, and
phase 1 is where it should be looked for.

`MAP_SHARED` between processes, and the copy-on-write behaviour of a private
file view. q5 maps a file into one process and reads it. It says nothing about
whether two processes mapping the same file share pages, which is the claim that
makes fifty processes' `libc.so` cost one copy, nor about what a write to a
private view does to the file underneath.

The image-section path. q5 uses data sections throughout, which is what section
4.4 describes. Whether a `SEC_IMAGE` section can replace a placeholder is
unasked, and it would matter only if the loader ever wanted NT's own image
mapping rather than its own layout of `PT_LOAD`s.
