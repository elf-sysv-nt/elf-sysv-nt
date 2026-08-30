/* WP-24 test support: a stand-in for the MS-ABI core the veneer repasses into.
 *
 * core.h is the contract; in the runtime WP-22 satisfies it out of the re-faced
 * Cygwin. Here each __core_v* is a Microsoft-ABI function that forwards to the
 * host runtime's own formatter -- which on this target is Microsoft-ABI already
 * -- so the test exercises a real convention crossing: a sysv_abi veneer entry,
 * a rebuilt Microsoft va_list, and a genuine Microsoft-ABI callee. This is the
 * "model the MS-ABI callee with the runtime's snprintf" the done-condition
 * allows, as spike 3 did.
 *
 * The formatters the test actually checks -- vfprintf, vsnprintf, the scanf
 * pair, the wide pair -- forward to the true function. The reporters and the
 * integer-only and fortified variants forward to a nearby real formatter; they
 * exist so the whole generated veneer links, and are not what the test asserts.
 */
#define _GNU_SOURCE 1	/* for vasprintf */
#include "../core.h"

#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#define MSABI __attribute__((ms_abi))

/* the checked formatters: the real thing */
MSABI int __core_vfprintf(FILE *f, const char *fmt, va_list ap) { return vfprintf(f, fmt, ap); }
MSABI int __core_vsprintf(char *s, const char *fmt, va_list ap) { return vsprintf(s, fmt, ap); }
MSABI int __core_vsnprintf(char *s, size_t n, const char *fmt, va_list ap) { return vsnprintf(s, n, fmt, ap); }
MSABI int __core_vdprintf(int fd, const char *fmt, va_list ap) { return vdprintf(fd, fmt, ap); }
MSABI int __core_vasprintf(char **sp, const char *fmt, va_list ap) { return vasprintf(sp, fmt, ap); }
MSABI char *__core_vasnprintf(char *b, size_t *n, const char *fmt, va_list ap) { return vasnprintf(b, n, fmt, ap); }
MSABI int __core_vfwprintf(FILE *f, const wchar_t *fmt, va_list ap) { return vfwprintf(f, fmt, ap); }
MSABI int __core_vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap) { return vswprintf(s, n, fmt, ap); }
MSABI int __core_vfscanf(FILE *f, const char *fmt, va_list ap) { return vfscanf(f, fmt, ap); }
MSABI int __core_vsscanf(const char *s, const char *fmt, va_list ap) { return vsscanf(s, fmt, ap); }
MSABI int __core_vfwscanf(FILE *f, const wchar_t *fmt, va_list ap) { return vfwscanf(f, fmt, ap); }
MSABI int __core_vswscanf(const wchar_t *s, const wchar_t *fmt, va_list ap) { return vswscanf(s, fmt, ap); }

/* variants forwarded to a nearby real formatter, for link completeness */
MSABI int __core_vfiprintf(FILE *f, const char *fmt, va_list ap) { return vfprintf(f, fmt, ap); }
MSABI int __core_vsiprintf(char *s, const char *fmt, va_list ap) { return vsprintf(s, fmt, ap); }
MSABI int __core_vsprintf_chk(char *s, int flag, size_t os, const char *fmt, va_list ap)
	{ (void)flag; (void)os; return vsprintf(s, fmt, ap); }
MSABI int __core_vsnprintf_chk(char *s, size_t n, int flag, size_t os, const char *fmt, va_list ap)
	{ (void)flag; (void)os; return vsnprintf(s, n, fmt, ap); }
MSABI int __core_vfscanf_r(struct _reent *r, FILE *f, const char *fmt, va_list ap)
	{ (void)r; return vfscanf(f, fmt, ap); }

/* the reporters: rendered to stderr, without the exit or errno tail the real
 * ones carry, since the test only needs them to link and to format */
MSABI void __core_verr(int e, const char *fmt, va_list ap) { (void)e; vfprintf(stderr, fmt, ap); }
MSABI void __core_verrx(int e, const char *fmt, va_list ap) { (void)e; vfprintf(stderr, fmt, ap); }
MSABI void __core_vwarn(const char *fmt, va_list ap) { vfprintf(stderr, fmt, ap); }
MSABI void __core_vwarnx(const char *fmt, va_list ap) { vfprintf(stderr, fmt, ap); }
MSABI void __core_vsyslog(int pri, const char *fmt, va_list ap) { (void)pri; (void)fmt; (void)ap; }
MSABI void __core_verror(int s, int e, const char *fmt, va_list ap) { (void)s; (void)e; vfprintf(stderr, fmt, ap); }
MSABI void __core_verror_at_line(int s, int e, const char *fl, unsigned int ln, const char *fmt, va_list ap)
	{ (void)s; (void)e; (void)fl; (void)ln; vfprintf(stderr, fmt, ap); }
MSABI void __core_vsetproctitle(const char *fmt, va_list ap) { (void)fmt; (void)ap; }
