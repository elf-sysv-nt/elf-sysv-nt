# DR-XXXX — the stat family does not forward, and filesystem crosses by its bind

Accepted 2026-09-01. Source: WP-56, the filesystem slice's live crossing.

## Context

DR-0055 fixed the rule that a SIGFE slice with no callable pure row crosses
live by its bind alone, and listed the SIGFE-heavy slices that inherit it. It
left filesystem off that list, on the reading that filesystem carries a callable
pure row a body-exercising crossing could use, the shape the ten crossings
before stdio took.

The filesystem crossing tested that reading and it does not hold. filesystem has
NOSIGFE, argument-only rows that look pure -- `fnmatch`, `alphasort`,
`versionsort` -- but none is stateless. `fnmatch` consults the locale's ctype
and collation; `alphasort` and `versionsort` run `strcoll` and `strverscmp`
over a `struct dirent`, standing on locale and on a header layout the crossing
discipline keeps out of a freestanding specimen. Calling `fnmatch` through its
generated thunk against the real DLL proved it directly: built byte-identical
and run three times, the five-check specimen returned three different verdicts
-- 30, then 5, then 31 -- the signature of a body reading uninitialised state.
`NOSIGFE` names the calling convention a thunk needs, not whether the body
behind it stands on its own, and filesystem is a slice with NOSIGFE rows and no
stateless one.

Binding the slice's table surfaced a second, larger fact. Of the 103 rows,
eleven do not resolve against a Cygwin-faced `elfsysv1.dll`. Ten are the stat
family: glibc's versioned wrappers `__xstat`, `__fxstat`, `__lxstat`,
`__xmknod`, their `*at` forms and their `*64` forms. These are the entry points
an el8 binary actually imports -- glibc keeps `stat` and its kin as header
inlines that call `__xstat(_STAT_VER, ...)`, so the symbol crossing the ABI is
`__xstat`, not `stat`. Cygwin has no such versioned-wrapper ABI: it exports the
plain `stat`, `fstat`, `lstat`, `fstatat`, `mknod` and `mknodat` (all present in
the DLL), and being LP64 with a single 64-bit `off_t` it carries no separate
`*64` symbol. The eleventh unresolved row is `getdirentries`, which Cygwin
exports neither as itself nor as `getdents`.

## Decision

filesystem crosses live by its bind alone, on the same terms as stdio under
DR-0055. This extends that rule: a slice inherits the bind-only crossing not
only when all its rows are SIGFE, but whenever it has no row whose body stands
on its own -- a slice with NOSIGFE rows every one of which reads locale, reent,
collation or a header-typed argument is crossed by its bind, because a
freestanding harness cannot call any of them without reading uninitialised
state. The live crossing certifies what such a harness can certify against a
real DLL and no more: the bind's shape, image-span containment, resolver
discrimination, distinct bodies, and idempotence.

The eleven unresolved rows are recorded as shim placeholders, not as wiring the
crossing accepts. Their export_name currently names the glibc symbol, which is a
generator placeholder; a real shim body for each drops glibc's version argument,
translates the `struct stat` layout, and calls the Cygwin function --
`__xstat`/`__xstat64` onto `stat`, `__fxstat`/`__fxstat64` onto `fstat`,
`__lxstat`/`__lxstat64` onto `lstat`, `__fxstatat`/`__fxstatat64` onto
`fstatat`, `__xmknod` onto `mknod`, `__xmknodat` onto `mknodat` -- with the
`*64` rows mapping onto the same call because Cygwin's types are already 64-bit.
`getdirentries` has no single Cygwin export behind it and is either composed
from `readdir`/`seekdir`/`telldir` or left a documented stub. Repointing these
eleven rows at their Cygwin targets, and writing the translating bodies, is the
filesystem slice's shim work; it is follow-on to this crossing, not part of it.

## Consequences

`t/live-filesystem.sh` (the fourteenth crossing) calls no filesystem body and
passes on the five bind properties, with its bind check encoding the finding: it
requires exactly the eleven stat-family and `getdirentries` rows to be null,
identified by name, and every other row filled. `bin/progress.py` and
`bin/build_status.py` count a slice crossed when its `live-<slice>.sh` is
present, so the bind-only crossing marks filesystem crossed exactly as the
body-exercising crossings marked their slices.

The crossing also refines DR-0055's list. That record named eleven SIGFE-heavy
slices as inheriting the bind-only rule and omitted filesystem; filesystem
inherits it too, by the extended criterion above, and a later reader should not
take DR-0055's omission to mean filesystem has a callable pure row. It does not.

A separate, narrower observation the crossing made, recorded here so it is not
lost: `fnmatch` is wired forward-same, but el8's `<fnmatch.h>` and Cygwin's
number the low flag bits in opposite order -- el8 has `FNM_PATHNAME` 0x01 and
`FNM_NOESCAPE` 0x02, Cygwin the reverse, while `FNM_PERIOD`, `FNM_LEADING_DIR`
and `FNM_CASEFOLD` agree and `FNM_NOMATCH` is 1 on both. The flagless call
crosses value-preserving, but an el8 caller passing `FNM_PATHNAME` reaches a
body that reads it as `FNM_NOESCAPE`, so `fnmatch` needs a shim that swaps the
two low bits. The crossing does not certify this against the body -- `fnmatch`
is not safely callable freestanding, as above -- so the flag-swap is left to
`diff-slice.sh`, where a differential against el8's own `fnmatch` will show it.

This record does not weaken filesystem's done-when. WP-56's per-slice bar is
still the differential against a real el8 userland; the live crossing was only
ever the added NT check, and for this slice that added check is the bind.
