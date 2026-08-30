/* Compiler-predefined standard macros for the veneer.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in COPYING.LESSER
   at the repository root.

   glibc ships a <stdc-predef.h> the compiler preincludes before any source is
   read, so it may not include <features.h> or anything that does.  This is the
   veneer's, authored rather than lifted: it carries no arithmetic, only the
   handful of __STDC_* facts a package may test for, set to the values el8's
   glibc 2.28 reports so that a probe gets the vendor's answer.

   __STDC_IEC_559__ and its complex form forward the compiler's own intent when
   the compiler states it (__GCC_IEC_559, gcc 4.9+), and default on otherwise,
   which is glibc's rule and the compiler's actual behaviour on this target.

   __STDC_ISO_10646__ is the Unicode version wchar_t is synchronised with.
   el8's glibc 2.28 reports 201706L (Unicode 10.0.0 / ISO 10646:2017), and the
   value is kept identical because a package that gates on it is asking what the
   library's locale data supports, which is the question the veneer answers with
   el8's data.  */

#ifndef	_STDC_PREDEF_H
#define	_STDC_PREDEF_H	1

#ifdef __GCC_IEC_559
# if __GCC_IEC_559 > 0
#  define __STDC_IEC_559__		1
# endif
#else
# define __STDC_IEC_559__		1
#endif

#ifdef __GCC_IEC_559_COMPLEX
# if __GCC_IEC_559_COMPLEX > 0
#  define __STDC_IEC_559_COMPLEX__	1
# endif
#else
# define __STDC_IEC_559_COMPLEX__	1
#endif

#define __STDC_ISO_10646__		201706L

#endif	/* stdc-predef.h  */
