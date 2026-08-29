/* Which named interfaces are not actually here.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../../LICENSE.

   glibc generates this per configuration and it is usually empty or nearly
   so, because on a real kernel almost everything is implemented.  Here it
   will not be empty, and that is the honest part of the header set: WP-52
   sorts every symbol in the map into four buckets, and the fourth is symbols
   with nothing behind them.  This file is that bucket, expressed where the
   preprocessor can see it.

   A __stub_ macro means the symbol is declared and links, and calling it sets
   errno to ENOSYS and fails predictably.  Autoconf has tested for these since
   the 1990s, so a package that checks gets the answer it knows how to handle
   rather than discovering it at run time.

   It is hand-written and nearly empty today because WP-52 has not run.  When
   it does, this file is generated from its fourth bucket and stops being
   hand-written; the shape is fixed here so that WP-14 has something to
   include and so that the generator has a target to produce.  */

#ifndef	_FEATURES_H
# error "Never include <gnu/stubs.h> directly; use <features.h> instead."
#endif

/* Nothing is listed yet.  That is not a claim that everything works; it is a
   statement that the classification has not been made, and a reader who takes
   an empty file here as an inventory has read it backwards.  WP-52 publishes
   the real one, and the plan calls that publication the most useful document
   this project will produce.  */
