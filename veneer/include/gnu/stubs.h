/* Which named interfaces are not actually here -- the veneer's own.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in COPYING.LESSER
   at the repository root.

   This is the one header in the set that is NOT el8's dropped in verbatim.
   DR-0000's copy line is that the veneer's headers are el8's glibc-headers-2.28
   vendored unchanged, and every other file here obeys it: features.h,
   sys/cdefs.h, stdc-predef.h and the bits/ files they pull in are byte-identical
   to el8's.  This file is the justified exception that record allows, because it
   is the one header whose correct contents differ from el8's by construction.

   el8's gnu/stubs.h reports which functions its glibc configuration left
   unimplemented -- on a real kernel, almost nothing, so el8's is effectively
   empty.  Here the answer is different and larger.  The veneer is a face over
   newlib plus Cygwin (DR-0000), so the functions with nothing behind them are
   the ones Cygwin's export surface does not carry, and WP-52 measures that set
   exactly: its fourth bucket is the 1797 symbols classified `stub', the ones a
   call to which can only fail predictably.  That inventory lives in
   veneer/classification/ (bucket4-inventory.tsv) and doc/what-the-veneer-lacks.md.
   Copying el8's answer here would be copying the wrong answer -- it would claim
   this platform implements what it does not -- so this header states our answer,
   and that is why it is a project file rather than a vendored one.

   A __stub_FUNCTION macro means the symbol is declared and links, and calling it
   sets errno to ENOSYS and fails predictably.  Autoconf has tested for these
   since the 1990s, so a package that checks gets the answer it knows how to
   handle rather than discovering it at run time.

   It is nearly empty today because WP-53 has not wired the fourth bucket through
   yet.  When it does, this file is generated from that bucket and stops being
   hand-written; the shape is fixed here so features.h has something to include
   and so the generator has a target to produce.  */

#ifndef	_FEATURES_H
# error "Never include <gnu/stubs.h> directly; use <features.h> instead."
#endif

/* Nothing is listed yet.  That is not a claim that everything works; it is a
   statement that WP-53 has not yet emitted the fourth bucket here, and a reader
   who takes an empty file as an inventory has read it backwards.  The real
   inventory is veneer/classification/bucket4-inventory.tsv and the document
   doc/what-the-veneer-lacks.md, which the plan calls the most useful document
   this project will produce.  */
