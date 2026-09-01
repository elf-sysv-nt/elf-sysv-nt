# DR-0053 — wchar_t is two bytes in the body, four in the face

Accepted 2026-09-01. Source: WP-56, the wchar slice's live crossing.

## Context

The wchar slice was wired as 87 forwards and no shims, and
`veneer/wiring/README.md` recorded the reason: "The wide-character surface is
value-preserving end to end: wchar_t is 4 bytes on both sides ... so every
row crosses as a tail jump." That was measured, but face-against-glibc: the
slice's diff cases ran with both sides on el8, and el8's `wchar_t` is four
bytes, the System V AMD64 width. They never met the real runtime.

The eleventh live crossing is the first time the wchar rows meet the real
`elfsysv1.dll`. The bind holds -- all 87 wide names are exported, `missing`
is zero -- so a resolved thunk reaches the real body. The body then moves by
its own idea of how wide a `wchar_t` is, and that idea is not four bytes.
`wmemcpy(dst, src, 5)` called through the wired thunk moves ten bytes, not
twenty: five two-byte elements. The measurement is byte-exact and
reproducing -- `t/live-wchar.sh`, status 31 -- and it holds at a second
length, `wmemcpy` of three elements moving six bytes, and through
`wmempcpy`, whose returned end pointer is the destination advanced by ten
bytes for five elements, the body striding in its own two-byte element.

This is not a surprise about Cygwin so much as one the earlier measurement
hid: `elfsysv1.dll` is Cygwin's newlib re-faced, and newlib on Windows uses
a two-byte `wchar_t`, the width Windows itself uses for `WCHAR`. The el8 face
this tree presents above the libc floor uses four. The two disagree, and
every wide value that crosses the boundary crosses that disagreement.

## Decision

The wchar slice is not a set of plain forwards, and the README's
"value-preserving, wchar_t is 4 bytes on both sides" is superseded by this
measurement. A `wchar_t` at the face is four bytes; the same `wchar_t` in the
body is two. Every row that passes, returns, or writes a `wchar_t`, a
`wchar_t *`, or a count of `wchar_t` needs a width-translating shim, not a
tail jump: narrowing four-byte face elements to the body's two on the way
down, widening two to four on the way up, and scaling any element count or
returned end pointer by the ratio.

This reclassifies the wchar slice from forward to shim. It is not edited to
say the widths agree; the wire table's `wire-wchar.gen.s` thunks remain, but
they are now the wrong body for the wide rows and are left in place only
until the shim generator that replaces them lands. The size and reach of that
generator -- which rows are pure width translation, which also touch a
conversion state or locale and were already fenced behind the SIGFE slices --
is scoped by a follow-up, not by this decision. What this decision fixes is
the fact the follow-up must build against: the widths differ, and the live
crossing pins where.

## Consequences

The wchar slice's demand rank (941, tenth of the sized slices) now buys shim
work rather than a generated forward, so the slice is more expensive than the
plan's forward premise implied. That cost is the honest one; a tail jump that
dropped the high half of every wide character would have been a silent
corruption in every el8 program that touches wide text, found not here but in
a vendor package's own test suite far downstream.

The finding is narrow to `wchar_t`'s width and does not touch the byte-string
slices: `char` is one byte on both sides, and string, stdio and the rest
cross as measured. It does reach beyond the wchar slice to any other row
elsewhere that carries a `wchar_t` -- the wide converters counted under wchar
already, but also any `char`/`wchar_t` conversion a locale row performs -- so
the width shim is a shared translation the locale bring-up will meet again,
not a wchar-only concern.

The live crossing that found this stays as its guard. It passes today by
confirming the two-byte stride; a later change that made the face and body
agree on width -- building the body with a four-byte `wchar_t`, or narrowing
the face -- would flip `t/live-wchar.sh` and announce that the shim is no
longer needed, rather than letting the reclassification rot into a rule
nobody rechecks.
