# An I/O call against a buffer the handler has not committed yet

Substrate N commits anonymous memory lazily, from a vectored handler on the
first touch (spike 37). Does `ReadFile` or `WriteFile` accept a user buffer
in such a range, with the handler installed and ready?

No. The I/O manager probes the buffer in kernel mode and returns
`ERROR_NOACCESS` (998) for a read into reserved, no-access or half-decommitted
memory and `ERROR_INVALID_USER_BUFFER` (1784) for a write from reserved
memory, on a synchronous and an overlapped handle alike, and the handler
that would have committed the page is never entered: zero hits in every case
against one hit for a user-mode touch of the same buffer, and the committed
control reads 64 KB both ways. `results-2026-09-06.txt` is the transcript;
`finding=io-probe-fails-uncommitted-buffer-handler-never-runs`.

## Why it matters

Proposal 0012 § 4 (DR-0098) makes every byte between user memory and a host
object go through `user_copy_*` and a kernel buffer, and gives the reason
under N as this behaviour, from a reading. The rule's justification under N
is now a measurement, and the door to zero-copy I/O under N is closed by the
kernel rather than by policy: a lazily committed user page cannot be handed
to the host, and a page the program `MADV_DONTNEED`ed (q7) cannot either.

**Gates.** DR-0098's N-side premise; 0012 § 4.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Native, `x86_64-w64-mingw32-gcc`, unelevated, under a second. The file read
is the probe's own executable; the file written is a delete-on-close temp.

## Method

A vectored handler commits any page of the current buffer on an access
violation, as the arena's does. q1 touches a reserved page from user mode
to show the handler works. q2 to q5 and q7 pass reserved, committed
`PAGE_NOACCESS`, and half-decommitted buffers to `ReadFile` and `WriteFile`
on synchronous and `FILE_FLAG_OVERLAPPED` handles and record the result, the
error and the handler's hit count. q6 is the committed control.

One thing the probe taught about itself: the buffer bounds the handler
checks are global pointers written just before the touch, and at `-O1` the
compiler moved that non-volatile store past the volatile touch, so the
handler saw no buffer and the process died. The pointers are declared
`*volatile` for that reason.

## What this does not reach

Only buffered file I/O on NTFS was tried; `FILE_NO_INTERMEDIATE_BUFFERING`,
AFD and ALPC probe the same way in principle and were not run. One Windows
build.
