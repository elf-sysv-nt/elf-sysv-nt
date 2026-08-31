# Licensing

Copyright (C) 2026 Philip Dye

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program.  If not, see <https://www.gnu.org/licenses/>.

The full texts are at the root: `COPYING.LESSER` is LGPLv3 and `COPYING` is
GPLv3,
which is the pair LGPLv3 is written against. `doc/decisions/0004-license.md`
is the record.

## Why this licence

It is inherited rather than chosen. Cygwin's API library, everything under
winsup/, is LGPLv3 or later, and this project rebuilds that library with a
different export face rather than replacing what is behind it.  A re-faced
library is a modified version of it, so LGPLv3+ attaches to the largest
component here by derivation.  doc/decisions/0004-license.md is the record.

Cygwin's linking exception does not reach that component and says so in its own
terms: it covers linking libcygwin.a, crt0.o and gcrt0.o with independent
modules, and defines an independent module as one not itself based on the
Cygwin library.


## No exception is granted here, yet

Cygwin grants an exception letting a linked executable be conveyed under terms
of the linker's choosing, without complying with LGPLv3 section 4.  That
exception is what lets a GPLv2-only program link Cygwin at all, since LGPLv3
and GPLv2-only do not otherwise combine, and this project intends to carry an
equivalent one forward for the same reason: el8 ships GPLv2-only software and
running it is the point of the exercise.

Whether a modified Cygwin library may carry that exception forward, and in what
wording, is a question for a lawyer and not for the people writing this.  Until
it is answered this repository grants no exception of its own.  The intent is
stated so that nobody plans around its absence; the wording is withheld so that
nobody relies on text an engineer invented.  A draft putting the question to
the Cygwin mailing list sits at `doc/proposals/licensing-email-draft.md`,
unsent as of 2026-08-30.


## Third-party material

Files under toolchain/ that are patches carry the licence of what they patch
rather than this one.  The GNU config patch is against GPLv3-with-exception
material and the flac patch against flac's own terms.

The headers under veneer/include/ are el8's glibc 2.28 installed headers,
vendored byte-identical per DR-0000 and DR-0010, with `gnu/stubs.h` the one
justified exception.  They are FSF-copyrighted, LGPL-2.1-or-later, and their
own notices travel with them; the copyright line above does not claim them.

Two inventories are cut from Cygwin 3.6.10 rather than written here:
`runtime/exports/cygwin-exports.tsv` from `cygwin.din` and
`runtime/imports/cygwin-imports.tsv` from the built DLL's import table, with
wrappers generated from both.  Beyond these, where a specification was
implemented rather than code lifted, the governing document says so.  An
earlier revision of this page said nothing in the tree was a copy of glibc or
Cygwin source; that was true when written and is not true now.
