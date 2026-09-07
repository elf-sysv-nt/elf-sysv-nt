# The veneer arc, and where it went

This repository begins at proposal 0011, which put a Linux kernel at the
syscall boundary. It is not where the project began. For the six weeks before
it, the same problem was attacked one layer up, at the C library: a veneer over
a re-faced Cygwin, presenting a glibc ABI that was not glibc. That approach is
finished, it is not coming back, and it is also not deleted.

It lives at **`https://github.com/elf-sysv-nt/elf-sysv-nt--veneer`**, public,
GPL, and complete — every commit, every decision record, every spike transcript
the arc produced. An offline copy of the tree at the moment of the split sits
beside it as
`elf-sysv-nt.veneer-over-cygwin.2026-09-05.tar.xz`, cut from `2b0d30d`.

## What this repository does not carry

The split was a filter, not a deletion, and three classes of thing stayed
behind because nothing here depends on them.

The code: `veneer/`, `loader/`, `runtime/winsup/`, `install/`, and the vendor
tree's `rhelcyg` branch. Proposal 0011 says it plainly — none of it is used.

The records that governed it. Forty-two decision records, DR-0000 among them,
settled questions about faces, wiring, version nodes, the low window, and an
errno floor that a single-numbering kernel makes vacuous. They were retired
together by the record that ratified 0011 rather than amended one at a time,
and a retired record is worth more sitting in the repository whose code it
governed than transplanted into one where it would describe nothing. DR-0004
and DR-0037, the licence pair, are the same story with a different cause: the
licence here is chosen rather than inherited, which DR-0095 records.

Twenty-three spike transcripts, the ones that characterised the veneer
bootstrap — the `reent-*` family, the face and version-node work, the Cygwin
build. Eighteen spikes did carry, because they measure this Windows rather than
that design, and the new design cites them.

## When to go looking

A citation in a surviving document that names a record or a path this tree does
not have is pointing at the sibling, not at a mistake. Read it there. The
history is one repository older than this one, and the two together are the
whole of it.
