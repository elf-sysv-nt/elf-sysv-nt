/* WP-24: the Microsoft-ABI core interface the variadic veneer repasses into.
 *
 * Each __core_v* here is the va_list-taking, Microsoft-ABI core of one family
 * of variadic exports. The veneer's job ends at building a Microsoft va_list;
 * the formatting itself belongs to the core, which is WP-22's to provide out
 * of the re-faced runtime. This header is the contract between the two: the
 * veneer names only these, never a public export, so a printf wrapper cannot
 * recurse into itself.
 *
 * The list argument is a plain (Microsoft) va_list -- the one this target's
 * default ABI already uses -- because that is exactly what __sv2ms_* rebuilds.
 * A stand-in that forwards each to the runtime's own implementation lives in
 * t/core.c; that is the "model the MS-ABI callee with the runtime's snprintf"
 * the done-condition allows, and it is what the test links.
 */
#ifndef ELFSYSV_RUNTIME_VARARGS_CORE_H
#define ELFSYSV_RUNTIME_VARARGS_CORE_H

#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

struct _reent;

#ifdef __cplusplus
extern "C" {
#endif

#define MSABI __attribute__((ms_abi))

/* narrow print */
MSABI int   __core_vfprintf(FILE *, const char *, va_list);
MSABI int   __core_vsprintf(char *, const char *, va_list);
MSABI int   __core_vsnprintf(char *, size_t, const char *, va_list);
MSABI int   __core_vdprintf(int, const char *, va_list);
MSABI int   __core_vasprintf(char **, const char *, va_list);
MSABI char *__core_vasnprintf(char *, size_t *, const char *, va_list);

/* narrow print, integer-only newlib variants (no floating conversions) */
MSABI int   __core_vfiprintf(FILE *, const char *, va_list);
MSABI int   __core_vsiprintf(char *, const char *, va_list);

/* narrow print, fortified */
MSABI int   __core_vsprintf_chk(char *, int, size_t, const char *, va_list);
MSABI int   __core_vsnprintf_chk(char *, size_t, int, size_t, const char *, va_list);

/* wide print */
MSABI int   __core_vfwprintf(FILE *, const wchar_t *, va_list);
MSABI int   __core_vswprintf(wchar_t *, size_t, const wchar_t *, va_list);

/* narrow scan */
MSABI int   __core_vfscanf(FILE *, const char *, va_list);
MSABI int   __core_vsscanf(const char *, const char *, va_list);
MSABI int   __core_vfscanf_r(struct _reent *, FILE *, const char *, va_list);

/* wide scan */
MSABI int   __core_vfwscanf(FILE *, const wchar_t *, va_list);
MSABI int   __core_vswscanf(const wchar_t *, const wchar_t *, va_list);

/* error and log reporters (all print-shaped) */
MSABI void  __core_verr(int, const char *, va_list);
MSABI void  __core_verrx(int, const char *, va_list);
MSABI void  __core_vwarn(const char *, va_list);
MSABI void  __core_vwarnx(const char *, va_list);
MSABI void  __core_vsyslog(int, const char *, va_list);
MSABI void  __core_verror(int, int, const char *, va_list);
MSABI void  __core_verror_at_line(int, int, const char *, unsigned int, const char *, va_list);
MSABI void  __core_vsetproctitle(const char *, va_list);

#undef MSABI

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_RUNTIME_VARARGS_CORE_H */
