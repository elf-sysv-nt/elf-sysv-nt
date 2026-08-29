/* Feature test macros, shaped as glibc shapes them.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../LICENSE.

   This is not glibc's features.h.  It is written to answer the questions
   glibc's answers, because several thousand packages ask them and branch on
   the reply, and because a package that probes the headers and then links the
   library has to get one answer rather than two.

   The version it reports is el8's, 2.28, and that is a claim about the
   interface rather than about the implementation.  What stands behind a given
   symbol at 2.28 is WP-52's four buckets, one of which is symbols that have
   nothing behind them at all.  A package reading __GLIBC_MINOR__ here learns
   which interfaces it may name, not which of them work; the honest inventory
   of the second thing is WP-52's to publish.  */

#ifndef	_FEATURES_H
#define	_FEATURES_H 1

/* The interface version.  el8 ships glibc 2.28 and this tree veneers onto its
   symbol set node for node, so a package that gates on the pair gets the same
   answer it would get on the vendor's own system.  WP-51 generates the map
   that makes the claim true.  */
#define	__GLIBC__		2
#define	__GLIBC_MINOR__		28

#define	__GLIBC_PREREQ(maj, min) \
	((__GLIBC__ << 16) + __GLIBC_MINOR__ >= ((maj) << 16) + (min))

/* GNU C library users, and the name this platform answers to.  __GNU_LIBRARY__
   is ancient and still tested for; 6 is what glibc has reported since 2.0.  */
#define	__GNU_LIBRARY__		6

/* Nothing else defines this, and config.guess does not see it -- the compiler
   defines __ELFSYSVNT__ for that, which is a different question asked at a
   different time.  This one is for source that wants to know at preprocess
   time which platform's headers it is reading.  */
#define	__ELFSYSVNT_HEADERS__	1

/* ---- feature test macros ------------------------------------------------
   The user sets _GNU_SOURCE and friends; this turns them into the __USE_*
   macros the rest of the headers actually test.  The precedence and the
   defaults follow glibc, deliberately and to the letter, because a header set
   that resolved them differently would compile the same source into a
   different program.  */

/* _GNU_SOURCE means all of them.  */
#ifdef _GNU_SOURCE
# undef  _ISOC95_SOURCE
# define _ISOC95_SOURCE		1
# undef  _ISOC99_SOURCE
# define _ISOC99_SOURCE		1
# undef  _ISOC11_SOURCE
# define _ISOC11_SOURCE		1
# undef  _POSIX_SOURCE
# define _POSIX_SOURCE		1
# undef  _POSIX_C_SOURCE
# define _POSIX_C_SOURCE	200809L
# undef  _XOPEN_SOURCE
# define _XOPEN_SOURCE		700
# undef  _XOPEN_SOURCE_EXTENDED
# define _XOPEN_SOURCE_EXTENDED	1
# undef  _LARGEFILE64_SOURCE
# define _LARGEFILE64_SOURCE	1
# undef  _DEFAULT_SOURCE
# define _DEFAULT_SOURCE	1
# undef  _ATFILE_SOURCE
# define _ATFILE_SOURCE		1
#endif

/* If nothing was asked for, glibc gives the default set rather than nothing.
   A source file that names no feature macro still expects strdup to be
   declared, and there is a great deal of such source.  */
#if (defined _DEFAULT_SOURCE					\
     || (!defined __STRICT_ANSI__				\
	 && !defined _ISOC99_SOURCE && !defined _ISOC11_SOURCE	\
	 && !defined _POSIX_SOURCE && !defined _POSIX_C_SOURCE	\
	 && !defined _XOPEN_SOURCE))
# undef  _DEFAULT_SOURCE
# define _DEFAULT_SOURCE	1
#endif

#ifdef _DEFAULT_SOURCE
# if !defined _POSIX_SOURCE && !defined _POSIX_C_SOURCE
#  define __USE_POSIX_IMPLICITLY	1
# endif
# undef  _POSIX_SOURCE
# define _POSIX_SOURCE		1
# undef  _POSIX_C_SOURCE
# define _POSIX_C_SOURCE	200809L
#endif

#if defined _POSIX_SOURCE || (defined _POSIX_C_SOURCE && _POSIX_C_SOURCE >= 1) \
    || defined _XOPEN_SOURCE
# define __USE_POSIX		1
#endif

#if defined _POSIX_C_SOURCE && _POSIX_C_SOURCE >= 2 || defined _XOPEN_SOURCE
# define __USE_POSIX2		1
#endif

#if defined _POSIX_C_SOURCE && (_POSIX_C_SOURCE - 0) >= 199309L
# define __USE_POSIX199309	1
#endif

#if defined _POSIX_C_SOURCE && (_POSIX_C_SOURCE - 0) >= 199506L
# define __USE_POSIX199506	1
#endif

#if defined _POSIX_C_SOURCE && (_POSIX_C_SOURCE - 0) >= 200112L
# define __USE_XOPEN2K		1
# undef  __USE_ISOC95
# define __USE_ISOC95		1
# undef  __USE_ISOC99
# define __USE_ISOC99		1
#endif

#if defined _POSIX_C_SOURCE && (_POSIX_C_SOURCE - 0) >= 200809L
# define __USE_XOPEN2K8		1
# undef  _ATFILE_SOURCE
# define _ATFILE_SOURCE		1
#endif

#ifdef _XOPEN_SOURCE
# define __USE_XOPEN		1
# if (_XOPEN_SOURCE - 0) >= 500
#  define __USE_XOPEN_EXTENDED	1
#  define __USE_UNIX98		1
#  undef  _LARGEFILE_SOURCE
#  define _LARGEFILE_SOURCE	1
#  if (_XOPEN_SOURCE - 0) >= 600
#   if (_XOPEN_SOURCE - 0) >= 700
#    define __USE_XOPEN2K8	1
#    define __USE_XOPEN2K8XSI	1
#   endif
#   define __USE_XOPEN2K	1
#   define __USE_XOPEN2KXSI	1
#   undef  __USE_ISOC95
#   define __USE_ISOC95		1
#   undef  __USE_ISOC99
#   define __USE_ISOC99		1
#  endif
# endif
#endif

#ifdef _LARGEFILE_SOURCE
# define __USE_LARGEFILE	1
#endif

#ifdef _LARGEFILE64_SOURCE
# define __USE_LARGEFILE64	1
#endif

#if defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64
# define __USE_FILE_OFFSET64	1
#endif

#if defined _DEFAULT_SOURCE || defined _ISOC99_SOURCE \
    || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L)
# define __USE_ISOC99		1
# define __USE_ISOC95		1
#endif

#if defined _ISOC11_SOURCE \
    || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L)
# define __USE_ISOC11		1
#endif

#ifdef _ATFILE_SOURCE
# define __USE_ATFILE		1
#endif

#ifdef _GNU_SOURCE
# define __USE_GNU		1
#endif

#ifdef _DEFAULT_SOURCE
# define __USE_MISC		1
#endif

#if defined _REENTRANT || defined _THREAD_SAFE
# define __USE_REENTRANT	1
#endif

/* Everything above is arithmetic on macros; the compiler-facing definitions
   live next door and are included last so that they can see the results.  */
#include <sys/cdefs.h>

/* Which of the interfaces named above are actually present.  glibc generates
   this file per configuration; here it is generated from WP-52's
   classification, and the fourth bucket -- symbols with nothing behind them --
   is what makes it worth having rather than empty.  */
#include <gnu/stubs.h>

#endif	/* features.h */
