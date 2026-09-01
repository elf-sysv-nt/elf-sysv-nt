# DR-0052 — a stub may be filled with a synthesized body, not only left to fail

Accepted 2026-08-31. Source: WP-56, the acceptance harness's first leaf.

## Context

WP-52 sorts every glibc symbol into four buckets by what the runtime has to
stand behind it: forward-same, forward-alias, shim, and stub. A stub is a
name Cygwin's export surface does not carry at all, and the classification's
own definition of the fourth bucket is "nothing behind it; becomes a stub
that fails predictably." Every stub so far has been read that way: the name
resolves to a body that reports its own absence rather than pretending to
work.

The acceptance harness's first pinned leaf, bzip2, cross-builds cleanly and
imports forty libc symbols. Thirty-nine forward or shim. One is a stub:
`__ctype_b_loc`, glibc's accessor for the character-class table its `ctype.h`
macros read through. Its two case-map kin, `__ctype_tolower_loc` and
`__ctype_toupper_loc`, and the hidden `__ctype_b` object they front, are
stubs for the same reason: they are glibc-internal, and Cygwin classifies
through newlib's own `_ctype_` table with a different bit layout, so nothing
on the export surface matches them by name. WP-52 filed all of them in
bucket 4, correctly.

But these are not a body that can only fail. What a caller reads through
`__ctype_b_loc` is pure data: a table, indexed `[-128..255]`, of the C
standard's own character classes, plus two case-map tables. The classes are
fixed by the language in the C and POSIX locales -- the locale every el8
build tool this harness targets runs in -- so the table is not Cygwin's to
provide or withhold. It can be synthesized from the standard's definitions
and checked, entry for entry, against what glibc's own tables hold. A stub
that fails predictably would be a strictly worse answer than the correct
table nobody has to translate.

Treating "absent from the export surface" as "must fail" would leave bzip2 --
and every program that classifies a character, which is nearly all of them --
permanently short one symbol for a table the project can compute exactly.

## Decision

A bucket-4 stub may be filled with a synthesized body when the body is
determinate data the veneer can produce and certify without a Cygwin export
to forward or translate. This is a fourth kind of wiring body, beside the
generated thunk (forward) and the hand-written translation (shim): a filled
stub, whose source is a generator and whose correctness is a differential
against the real reference, not a Cygwin call it stands in front of.

The classification is not edited to say otherwise. `__ctype_b_loc` and its
kin remain bucket-4 in `classification.tsv`, because that table records one
fact -- whether the export surface carries the name -- and it does not. The
filled body lives in the wiring layer as a shared component beside
`xlat-core.gen.c`, not in a slice's bind table, since it forwards to nothing.
What must still change, and is left to a follow-up, is how the acceptance
harness reports such a symbol: a stub the veneer fills is not a stub that
fails, and the verdict should come to distinguish them rather than counting
this row against bzip2 forever.

The first filled stub is the ctype family. `gen-ctype-table.py` emits the
three tables, synthesized from the C standard's class and case definitions
and bound to their `GLIBC_2.3` accessors; `t/ctype-table.sh` pins the output
byte-identical to its generator and certifies all 384 entries of all three
tables byte-for-byte against glibc's real ones, run on the pinned el8 image.

## Consequences

The scope is the C and POSIX locales. A program that sets a non-C locale and
then classifies bytes above the ASCII range would read this table where
glibc would read a locale-specific one; the two agree only in the C locale.
That is the same seam every other slice draws -- category-sensitive behaviour
belongs downstream of the bind -- and closing it means translating Cygwin's
own per-locale ctype data into glibc's bit layout at load, which this static
body does not attempt and which no el8 build tool this harness targets needs.

A filled stub widens the definition of what the fourth bucket's members can
become, and a later reader could take it as licence to invent a body for any
absent name. The line is determinacy: the ctype tables are fixed by the
language and checkable against the reference. A stub whose behaviour depends
on state the veneer does not have -- a real system call, a Cygwin-side object
-- is not a candidate, and stays a stub that fails until something genuine
stands behind it.
