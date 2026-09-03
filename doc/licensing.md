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

## The linking exception

Cygwin's linking exception lets an executable that links the library be
conveyed under terms of the linker's choosing, without complying with LGPLv3
section 4.  That exception is what lets a GPLv2-only program link Cygwin at
all, since LGPLv3 and GPLv2-only do not otherwise combine, and el8 ships
GPLv2-only software whose running is the point of the exercise.

The exception carries forward with this modified library.  Under GPLv3
section 7 an additional permission travels with the work unless a conveyor
removes it, nothing in the exception's text terminates it on modification,
and this is the reading the ecosystem already operates on: MSYS2 ships the
exception verbatim with its own modified Cygwin runtime, and Git for Windows
has distributed GPLv2-only git over `msys-2.0.dll` for a decade on the same
footing.  DR-0037 records the decision and the precedent.  Executables
linking `elfsysv1.dll` are conveyed under the exception the way executables
linking `cygwin1.dll` are.

The text is upstream's, reproduced verbatim and unaltered from
https://cygwin.com/licensing.html; nothing here is wording of this project's
invention:

> As a special exception, the copyright holders of the Cygwin library grant
> you additional permission to link libcygwin.a, crt0.o, and gcrt0.o with
> independent modules to produce an executable, and to convey the resulting
> executable under terms of your choice, without any need to comply with the
> conditions of LGPLv3 section 4. An independent module is a module which is
> not itself based on the Cygwin library.


## Lifts

A lift is cleared by licence text and recorded practice, per DR-0074: the
file's own licence header, the FSF's published compatibility guidance, and a
named project that has done the same combination in public, at a ref somebody
here read, recorded in a Precedent section shaped like DR-0037's.  Counsel is
not in the loop, and every such record's Not verified section says what that
means: practice is acquiescence rather than confirmation.  A lift that can
produce no precedent is not blocked; it is recorded as resting on text alone.

LGPL-2.1-or-later material may be taken into the shipped runtime and conveyed
under this tree's LGPLv3+, on LGPL-2.1 section 13's own terms; glibc is in
that class, and the vendored headers below already stand on it under DR-0010.
LGPL-2.1-only has no path to version 3 and does not combine — read the file
header, not the project's reputation.  Each lift retains its notices
unmodified, adds a row to the third-party section below, pins the upstream
ref, and declares in the governing document whether the material is used,
adapted, or written from specification.

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
