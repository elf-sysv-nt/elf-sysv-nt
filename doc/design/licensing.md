# Licensing

Copyright (C) 2026 Philip Dye

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2.1 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.

The full text is at the root, in `COPYING.LESSER`.

## Why this licence

It was chosen. The kernel takes no code from Cygwin, so no inheritance reaches
this tree; what it borrows from elsewhere is ideas and on-disk formats, which
DR-0074's check distinguishes from lifting. LGPL-2.1-or-later is glibc's, and
glibc is what every program this kernel runs already links against, so the
licence asks nothing of an integrator that the C library has not asked already.

The veneer arc licensed itself by derivation instead, under DR-0004 and
DR-0037. Those records describe a different tree, they were not amended, and
they live in the sibling repository with the code they governed:
`doc/history/the-veneer-arc.md` says where.

Settled by: DR-0095, DR-0074.
