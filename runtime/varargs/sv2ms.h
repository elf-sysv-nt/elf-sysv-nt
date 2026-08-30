/* WP-24: the System V variadic surface over a Microsoft-ABI core.
 *
 * The runtime is compiled to the System V AMD64 ABI; the core formatters it
 * calls down into are compiled to the Microsoft x64 ABI. A variadic call
 * cannot be forwarded across that seam: a System V va_list is a twenty-four-
 * byte descriptor (gp_offset, fp_offset, overflow_arg_area, reg_save_area)
 * and a Microsoft va_list is an eight-byte pointer walked in place. Handing
 * one to a reader shaped for the other reads the descriptor's header as the
 * first argument -- spike 3 measured 206158430216 where 111 was passed.
 *
 * So every variadic entry point walks its own System V list and rebuilds a
 * Microsoft one, then hands that to the MS-ABI core. The rebuild is driven by
 * the format string, because a format is the only thing that says which of a
 * variadic argument's two homes -- an integer register file or a floating one
 * -- a given argument came from; nothing generic can recover that from the
 * bytes. This header is the interface; sv2ms.c is the walk.
 */
#ifndef ELFSYSV_RUNTIME_VARARGS_SV2MS_H
#define ELFSYSV_RUNTIME_VARARGS_SV2MS_H

#include <stddef.h>
#include <stdarg.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The System V va_list, named apart from the default (Microsoft) one. On this
 * target plain va_start in a sysv_abi function is a category error, so the
 * veneer uses these builtins for every list it walks. */
typedef __builtin_sysv_va_list	__sysv_va_list;
#define __sysv_va_start(ap, last)	__builtin_sysv_va_start(ap, last)
#define __sysv_va_arg(ap, type)		__builtin_va_arg(ap, type)
#define __sysv_va_end(ap)		__builtin_sysv_va_end(ap)
#define __sysv_va_copy(dst, src)	__builtin_va_copy(dst, src)

/* The rebuilt Microsoft list is a flat run of eight-byte slots: every scalar a
 * printf or scanf conversion consumes -- int, long, long long, double, any
 * pointer -- occupies exactly one slot under the Microsoft x64 ABI, which is
 * what makes the rebuild a copy of values rather than a re-encoding. The one
 * scalar that does not is long double, whose representation itself differs
 * between the two ABIs; see sv2ms.c and the README.
 *
 * The bound is the most conversions one call will ever carry. A slot is eight
 * bytes, so this is a one-kilobyte stack buffer, refused rather than
 * overflowed if a pathological format exceeds it. */
#define VARARGS_MAX_SLOTS	128

/* Walk a System V va_list against a printf-family format and lay the arguments
 * out as a Microsoft va_list in slots[], returning a va_list positioned at its
 * head. slots must have room for VARARGS_MAX_SLOTS and outlive the core call.
 * On a format that would need more than max slots the excess is dropped and
 * the truncation is the caller's to notice; a correct format never reaches it.
 *
 * The narrow and wide forms differ only in the width of the format's own
 * characters; the arguments they describe are identical. The scan forms lay
 * out one pointer slot per non-suppressed conversion. */
va_list	__sv2ms_print(unsigned long long *slots, int max,
		      const char *fmt, __sysv_va_list ap);
va_list	__sv2ms_wprint(unsigned long long *slots, int max,
		       const wchar_t *fmt, __sysv_va_list ap);
va_list	__sv2ms_scan(unsigned long long *slots, int max,
		     const char *fmt, __sysv_va_list ap);
va_list	__sv2ms_wscan(unsigned long long *slots, int max,
		      const wchar_t *fmt, __sysv_va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_RUNTIME_VARARGS_SV2MS_H */
