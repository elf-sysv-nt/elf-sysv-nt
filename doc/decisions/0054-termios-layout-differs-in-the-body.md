# DR-0054 — struct termios is laid out differently in the body than the face

Accepted 2026-09-01. Source: WP-56, the terminal slice's live crossing.

## Context

The terminal slice was wired as forwards, one shim (`ioctl`), and its diff
cases ran with both sides on el8, glibc against glibc, as the earlier slices
did. Three of its rows are marked NOSIGFE and call cleanly from a freestanding
harness: `cfgetispeed`, `cfgetospeed` and `cfmakeraw`. The first two read a
`speed_t` out of a caller-owned `struct termios`; the third clears bits in the
same struct's leading flag words.

The twelfth live crossing is the first time the terminal rows meet the real
`elfsysv1.dll`. The bind holds -- every terminal name, `ioctl` included, is
exported, `missing` is zero -- so a resolved thunk reaches the real body. The
body then reads the caller's `struct termios` by its own idea of where the
fields lie, and the two ideas of `NCCS` differ. Measured with each side's own
headers:

              c_iflag c_oflag c_cflag c_lflag c_line c_cc NCCS c_ispeed c_ospeed size
    face(el8)     0       4       8      12      16    17   32     52       56     60
    body(cyg)     0       4       8      12      16    17   18     36       40     44

The leading flag words and the start of `c_cc` share their offsets exactly.
The divergence is `NCCS`: the face's `c_cc` is thirty-two bytes, the body's is
eighteen, so `c_ispeed` and `c_ospeed` sit at 52/56 in the face and 36/40 in
the body. `cfgetispeed` and `cfgetospeed` in the body are plain field reads --
`return t->c_ispeed;`, measured on Cygwin -- so called on a face-laid struct
they read offset 36/40, the body's field, not 52/56, the face's. The
measurement is byte-exact and reproducing: `t/live-terminal.sh`, status 31,
with `cfgetispeed` returning the value placed at the body's offset and not the
one at the face's, `cfgetospeed` reproducing it on the output speed, and a
control confirming the body never consults the face's offset at all.

`cfmakeraw` is the other half of the finding. It touches only the leading flag
words the two layouts share -- and `c_cc[VMIN]`/`c_cc[VTIME]`, which start at
the shared `c_cc` -- so it crosses value-preserving on that shared region:
from an all-ones struct the body leaves `c_iflag=0xfffefa1c`,
`c_oflag=0xfffffffe`, `c_cflag=0xfffffeff`, `c_lflag=0xfffffed8`, and a
face-laid struct reads those back at the same offsets. The divergence is not
the whole struct; it is the tail that `NCCS` displaces.

This is the same shape of surprise the wchar crossing found (DR-0053), one
level up: there the element width differed, here the aggregate layout does.
Both come from the same root -- `elfsysv1.dll` is Cygwin's newlib re-faced,
and newlib's `struct termios` is not el8's -- and both were hidden by diff
cases that ran glibc against glibc.

## Decision

The terminal slice's `struct termios`-bearing rows are not plain forwards. Any
row that reads or writes a field past the shared leading region of `struct
termios` -- which is every row touching `c_cc` beyond its start, `c_ispeed`,
or `c_ospeed`, so `cfgetispeed`, `cfgetospeed`, `cfsetispeed`, `cfsetospeed`,
`tcgetattr`, `tcsetattr` and their kin -- needs a layout-translating shim, not
a tail jump: a shim that maps the face's sixty-byte, `NCCS`-32 `struct termios`
to the body's forty-four-byte, `NCCS`-18 one on the way down, and back on the
way up.

`cfmakeraw` and any row confined to the shared leading flag words may stay a
forward; the crossing pins that those offsets coincide. The boundary the shim
must cover is the tail past `c_cc[18]`, not the whole struct.

This reclassifies the struct-bearing part of the terminal slice from forward to
shim. As with the wchar reclassification, the wire table's
`wire-terminal.gen.s` thunks remain in place only until the layout shim that
replaces them lands; they are now the wrong body for the struct-reading rows.
The size of that shim generator, and whether it is shared with the other
struct-bearing slices the census still has ahead (the layout translation is not
terminal's alone), is scoped by a follow-up, not by this decision. What this
decision fixes is the fact the follow-up must build against: the `struct
termios` the face presents and the one the body reads are not the same bytes,
and the live crossing pins where they part.

## Consequences

The terminal slice's demand rank (1910, seventh of the sized slices) now buys
layout-shim work rather than a generated forward for its struct-bearing rows,
so the slice is more expensive than the plan's forward premise implied. That
cost is the honest one: a tail jump that read `c_ispeed` from the face's offset
would have handed every el8 program that queries a terminal's speed a value
from fourteen bytes into the wrong field, found not here but in a vendor
package's own test suite far downstream.

The finding generalizes past terminal. `struct termios` is one aggregate whose
`NCCS` the two libcs size differently; it is unlikely to be the last. Any slice
whose rows cross a struct the face and body both define -- and the census has
several ahead -- must be crossed against the real DLL before its struct rows
are trusted as forwards, exactly as this crossing did, rather than certified on
glibc-against-glibc diff cases alone. The layout shim the terminal rows need is
plausibly the first instance of a translation the later struct-bearing slices
will meet again.

The live crossing that found this stays as its guard. It passes today by
confirming the body reads its own `NCCS`-18 offsets; a later change that made
the face and body agree on `struct termios` -- building the body with a
thirty-two-entry `c_cc`, or narrowing the face's -- would flip
`t/live-terminal.sh` and announce that the layout shim is no longer needed,
rather than letting the reclassification rot into a rule nobody rechecks.
