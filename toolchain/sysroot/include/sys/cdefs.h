/* Compiler-facing definitions, shaped as glibc shapes them.
   Copyright (C) 2026 Philip Dye.  Distributed under the terms in ../../../LICENSE.

   Almost every header in the set expands these, and a surprising amount of
   third-party source expands them directly -- __BEGIN_DECLS and __THROW turn
   up in packages that never meant to depend on glibc internals.  So the
   spellings matter more than the contents.  */

#ifndef	_SYS_CDEFS_H
#define	_SYS_CDEFS_H 1

#ifndef _FEATURES_H
# include <features.h>
#endif

#if defined __cplusplus
# define __BEGIN_DECLS	extern "C" {
# define __END_DECLS	}
#else
# define __BEGIN_DECLS
# define __END_DECLS
#endif

/* GCC is the only compiler this target has, and it is ours, so the version
   gates below are about which GCC rather than about which vendor.  */
#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define __GNUC_PREREQ(maj, min) 0
#endif

#define __glibc_clang_prereq(maj, min) 0

#ifdef __GNUC__
# define __THROW	__attribute__ ((__nothrow__ __LEAF))
# define __THROWNL	__attribute__ ((__nothrow__))
# define __NTH(fct)	__attribute__ ((__nothrow__ __LEAF)) fct
# define __NTHNL(fct)	__attribute__ ((__nothrow__)) fct
#else
# define __THROW
# define __THROWNL
# define __NTH(fct)	fct
# define __NTHNL(fct)	fct
#endif

/* leaf is a promise that the function will not call back into this unit.  It
   is worth keeping honest here rather than defining it away, because the
   runtime beneath this veneer does call back -- a signal delivered inside a
   libc call reaches an ELF-side handler -- and a wrongly-leaf declaration
   would license the compiler to cache across exactly that.  */
#if __GNUC_PREREQ (4, 6)
# define __LEAF		, __leaf__
# define __LEAF_ATTR	__attribute__ ((__leaf__))
#else
# define __LEAF
# define __LEAF_ATTR
#endif

#define __ptr_t		void *

#if __GNUC_PREREQ (3, 2)
# define __attribute_const__	__attribute__ ((__const__))
# define __attribute_pure__	__attribute__ ((__pure__))
# define __attribute_malloc__	__attribute__ ((__malloc__))
# define __attribute_used__	__attribute__ ((__used__))
# define __attribute_noinline__	__attribute__ ((__noinline__))
# define __attribute_deprecated__ __attribute__ ((__deprecated__))
# define __attribute_format_arg__(x) __attribute__ ((__format_arg__ (x)))
# define __attribute_format_strfmon__(a, b) \
	__attribute__ ((__format__ (__strfmon__, a, b)))
# define __attribute_warn_unused_result__ \
	__attribute__ ((__warn_unused_result__))
#else
# define __attribute_const__
# define __attribute_pure__
# define __attribute_malloc__
# define __attribute_used__
# define __attribute_noinline__
# define __attribute_deprecated__
# define __attribute_format_arg__(x)
# define __attribute_format_strfmon__(a, b)
# define __attribute_warn_unused_result__
#endif

#define __nonnull(params)	__attribute__ ((__nonnull__ params))
#define __wur			__attribute_warn_unused_result__

#if __GNUC_PREREQ (4, 3)
# define __attribute_alloc_size__(params) \
	__attribute__ ((__alloc_size__ params))
#else
# define __attribute_alloc_size__(params)
#endif

#define __restrict_arr	__restrict

#define __glibc_unlikely(cond)	__builtin_expect ((cond), 0)
#define __glibc_likely(cond)	__builtin_expect ((cond), 1)

#define __extern_inline		extern __inline __attribute__ ((__gnu_inline__))
#define __extern_always_inline \
	extern __always_inline __attribute__ ((__gnu_inline__))
#define __always_inline		__inline __attribute__ ((__always_inline__))

/* Symbol renaming, which is the mechanism the whole veneer rests on: a
   declaration here can bind a name to a differently-named body without the
   caller knowing.  WP-52's first two buckets are exactly the symbols that
   forward under another name and under the same one.  */
#define __REDIRECT(name, proto, alias) name proto __asm__ (__ASMNAME (#alias))
#define __REDIRECT_NTH(name, proto, alias) \
	name proto __asm__ (__ASMNAME (#alias)) __THROW
#define __REDIRECT_NTHNL(name, proto, alias) \
	name proto __asm__ (__ASMNAME (#alias)) __THROWNL
#define __ASMNAME(cname)	__ASMNAME2 (__USER_LABEL_PREFIX__, cname)
#define __ASMNAME2(prefix, cname) __STRING (prefix) cname

#define __STRING(x)	#x
#define __CONCAT(x, y)	x ## y

/* The ABI this target is, stated where source can test it.  Both are true at
   once and that is the point of the project: System V above, and a runtime
   below that is not a kernel.  */
#define __ELF__			1
#define __SYSV_ABI__		1

/* No kernel means no vDSO and no syscall instruction reaching one.  Source
   that would have gone through either has to go through the runtime, and
   this is the macro that lets it find out at compile time rather than at the
   first fault.  */
#define __NO_SYSCALL_INTERFACE__ 1

#define __LDBL_REDIR(name, proto)	name proto
#define __LDBL_REDIR_NTH(name, proto)	name proto __THROW
#define __LDBL_REDIR1(name, proto, alias) name proto
#define __LDBL_REDIR_DECL(name)

#define __fortify_function	__extern_always_inline __attribute_artificial__

#if __GNUC_PREREQ (4, 3)
# define __attribute_artificial__ __attribute__ ((__artificial__))
#else
# define __attribute_artificial__
#endif

#define __glibc_c99_flexarr_available 1
#define __flexarr	[]

#endif	/* sys/cdefs.h */
