# DR-0011 — the loader's cache is this project's own format, not glibc's

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: the WP-33 agent, on a defensible call the operator may revisit
Proposal: none; taken when WP-33 needed a cache to search

## What was decided

WP-33's `ldconfig` cache is a format this project defines, described in
`loader/graph/ldso_cache.h`: a header carrying a magic and version, an array of
entries sorted by soname, and a string table, with soname and path offsets
into that table. It is not glibc's binary `/etc/ld.so.cache`, neither the old
`ld.so.cache1.0` layout nor the `glibc-ld.so.cache1.1` extension el8 writes.
The reader validates every offset against the file's own bounds before it
dereferences it and requires the entries to be sorted, so a truncated or
self-inconsistent cache is refused with a code rather than trusted, the same
discipline WP-31 holds the ELF parser to.

## Why our own format

The cache participates in one thing at this stage: the object graph's name
search, between `DT_RUNPATH` and the default directories. Nothing else in the
tree reads it. glibc's on-disk cache is shaped by history this project does not
share — two concatenated layouts for backward compatibility, a flags word whose
bits encode ABIs and hardware capabilities that predate x86-64, and an
extension section — and reproducing it byte for byte would be work in service
of a compatibility nobody is asking for, since the only reader is our own
loader. A format we define is a few fields, is validated the way the rest of
the loader validates its input, and is enough to answer the search.

The done-when for WP-33 is that the load order matches a real `ld.so`, and the
cases where a real `ld.so` gives a checkable answer are the ones the search
precedence turns on — rpath, `LD_LIBRARY_PATH`, runpath, and the default path.
The cache is our fourth source and is exercised against a known-answer
construction rather than against glibc's cache, because getting a real `ld.so`
to read an arbitrary cache file is not something its interface offers. So
binary compatibility with glibc's cache would not have bought a stronger test
either.

## What it does not decide

Whether the shipped platform ever needs glibc's cache format. WP-62 is where rpm
and its tooling meet this loader, and if some vendor tool reads
`/etc/ld.so.cache` directly rather than going through `ld.so` — a possibility
that record can weigh when it has the tool in front of it — then a writer that
emits glibc's layout is added there, beside this one, for that consumer. That is
a new writer for an external reader, not a change to how this loader searches,
so it does not reopen this record; it would be its own. This record claims only
that the loader's *internal* cache need not be glibc-shaped.

The cache's precedence rule among duplicate sonames. The builder keeps the file
from the directory scanned last, and the tool leaves the scan order to its
caller rather than encoding glibc's own version-preference heuristic. That is a
policy the tool owns and can refine — preferring the greater version, say —
without touching the format, so it is noted here rather than settled.

## What it costs to reverse

Cheap. The reader and writer are one small translation unit behind an interface
the walker uses through three functions, and the walker never sees the bytes.
Replacing the format with glibc's, or adding glibc's as a second output, is a
change to `ldso_cache.c` and `ldconfig.c` and to nothing that calls them.
Reversal, if the operator wants the internal cache glibc-shaped after all, is a
new record pointing back here.

## Where it is written down

`loader/graph/ldso_cache.h`, which defines the layout, and `loader/graph/ldso_cache.c`,
which reads and writes it. `loader/graph/README.md`, under "The cache and its
tool", points here. `doc/IMPLEMENTATION-PLAN.md`, WP-33, where the delivery note
cites this record.
