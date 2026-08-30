# DR-0017 — version-node identity objects are scaffold, not a fourth-bucket stub

Status: accepted
Date: 2026-08-30
Deciding: the WP-52 agent, on a defensible call the operator may revisit
Proposal: none; taken when WP-52 partitioned the version map

## What was decided

WP-52 sorts every symbol in the version map into four buckets: forwards under
another name, forwards under the same name, needs a shim, or has nothing behind
it and becomes a stub. The map also contains 68 rows whose symbol name is a
version string — `GLIBC_2.10`, `GLIBC_2.14`, `XCRYPT_2.0` and the rest — each a
zero-content `object` in `.dynsym` that a versioned library carries as the
identity of a version node. These 68 rows are classified into a fifth
disposition, `scaffold`, and not into any of the four buckets. The four
functional buckets partition the remaining 3956 rows; the reproduce test asserts
all five dispositions together cover the map with no row unclassified.

## Why not a bucket

The obvious mechanical answer is bucket 4: the runtime exports nothing named
`GLIBC_2.14`, so by the same absence rule that files `epoll_wait` as a stub, the
version anchor is absent too. That answer is wrong in the way that matters.

Bucket 4 is not merely "absent from the export surface." Its definition is a
symbol that "becomes a stub that fails predictably" — a callable a program can
reach that will fault or error when it does. A version-node identity object is
not callable and no program reaches it. It exists so the dynamic linker can bind
`memcpy@GLIBC_2.14` to a node; it is emitted by the linker from the version
script, and on this platform WP-53 re-emits it when it reconstructs each
library's `.gnu.version_d` from WP-51's node ladder. There is something behind
it — the veneer's own versioning — and it is neither a forward to a Cygwin
export nor a failing stub.

Filing it in bucket 4 would also damage the one artifact the fourth bucket exists
to be. That bucket is published as `doc/what-the-veneer-lacks.md`, the honest
inventory of what a package cannot call here. Sixty-eight version strings listed
among `epoll_wait`, `argp_parse` and `backtrace` would be 68 entries of noise in
the document whose whole value is that every line is a real thing a real package
would miss. The inventory is more honest with them removed than with them padded
in.

## Why a named disposition and not a silent drop

The WP-52 bar is that the four buckets "partition the map with no symbol
unclassified." Dropping the 68 rows before classifying would meet the letter of a
four-bucket partition while quietly shrinking the domain, which is the kind of
move the bar exists to forbid. So the rows are kept and given an explicit
disposition that is visible in `classification.tsv` and counted in every summary.
Nothing is unclassified; one class simply is not one of the four functional
buckets, and says so by name. A reader who wants the strict four-way split of the
API provides has it; a reader auditing completeness sees all 4024 rows accounted.

## Scope

This decides only how the version-node objects are dispositioned in WP-52's
partition. It does not touch WP-51's map, which correctly records them as the
vendor's `.dynsym` carries them, nor WP-53's reconstruction of the version
definitions from the node ladder, which is where these anchors are actually
re-emitted. Whether `libcrypt.so.1`'s `XCRYPT_2.0` node is ultimately provided
remains the WP-53/WP-54 scope call DR-0013 left open; carrying it as scaffold
here does not settle it.

## When to revisit

If WP-53's reconstruction turns out to emit these anchors from the classification
rather than from the node ladder, the disposition should move to wherever that
work reads it, and this record should be superseded rather than edited.
