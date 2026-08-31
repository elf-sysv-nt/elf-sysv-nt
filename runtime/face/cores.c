/* WP-27: the MS-ABI core the variadic veneer repasses into, bound to the
 * DLL's own formatters.
 *
 * core.h is the contract WP-24's generated veneer calls through. Inside
 * elfsysv1.dll every body is Microsoft-ABI and this translation unit is
 * compiled by the host gcc, so each bind is a plain forwarding call: the
 * same signature, the same va_list, the callee reached by its DLL-internal
 * name. Twenty-one cores forward one-to-one. The four Cygwin keeps static
 * (its verror, verror_at_line, vsetproctitle) or does not carry at all
 * (vsiprintf) are written here in terms of what the DLL does define.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <err.h>
#include <syslog.h>

#include "../varargs/core.h"

/* core.h undefines its own MSABI on the way out */
#define MSABI __attribute__((ms_abi))

/* DLL-internal names the host headers do not declare. Everything here is
 * Microsoft-ABI, this unit's default. */
extern int __vsprintf_chk (char *, int, size_t, const char *, va_list);
extern int __vsnprintf_chk (char *, size_t, int, size_t, const char *, va_list);
extern int _vfscanf_r (struct _reent *, FILE *, const char *, va_list);
extern int vfiprintf (FILE *, const char *, va_list);
extern void setproctitle (const char *, ...);

/* errno.h declares program_invocation_name dllimport for outside callers;
 * this unit lives inside the DLL, so bind the name directly instead. */
extern char *core_progname __asm__ ("program_invocation_name");

/* the glibc-compat error() state the DLL exports */
extern unsigned int error_message_count;
extern int error_one_per_line;
extern void (*error_print_progname) (void);

/* one-to-one forwards */
MSABI int __core_vfprintf (FILE *f, const char *fmt, va_list ap) { return vfprintf (f, fmt, ap); }
MSABI int __core_vsprintf (char *s, const char *fmt, va_list ap) { return vsprintf (s, fmt, ap); }
MSABI int __core_vsnprintf (char *s, size_t n, const char *fmt, va_list ap) { return vsnprintf (s, n, fmt, ap); }
MSABI int __core_vdprintf (int fd, const char *fmt, va_list ap) { return vdprintf (fd, fmt, ap); }
MSABI int __core_vasprintf (char **sp, const char *fmt, va_list ap) { return vasprintf (sp, fmt, ap); }
MSABI char *__core_vasnprintf (char *b, size_t *n, const char *fmt, va_list ap) { return vasnprintf (b, n, fmt, ap); }
MSABI int __core_vfiprintf (FILE *f, const char *fmt, va_list ap) { return vfiprintf (f, fmt, ap); }
MSABI int __core_vsprintf_chk (char *s, int flag, size_t len, const char *fmt, va_list ap) { return __vsprintf_chk (s, flag, len, fmt, ap); }
MSABI int __core_vsnprintf_chk (char *s, size_t n, int flag, size_t len, const char *fmt, va_list ap) { return __vsnprintf_chk (s, n, flag, len, fmt, ap); }
MSABI int __core_vfwprintf (FILE *f, const wchar_t *fmt, va_list ap) { return vfwprintf (f, fmt, ap); }
MSABI int __core_vswprintf (wchar_t *s, size_t n, const wchar_t *fmt, va_list ap) { return vswprintf (s, n, fmt, ap); }
MSABI int __core_vfscanf (FILE *f, const char *fmt, va_list ap) { return vfscanf (f, fmt, ap); }
MSABI int __core_vsscanf (const char *s, const char *fmt, va_list ap) { return vsscanf (s, fmt, ap); }
MSABI int __core_vfscanf_r (struct _reent *r, FILE *f, const char *fmt, va_list ap) { return _vfscanf_r (r, f, fmt, ap); }
MSABI int __core_vfwscanf (FILE *f, const wchar_t *fmt, va_list ap) { return vfwscanf (f, fmt, ap); }
MSABI int __core_vswscanf (const wchar_t *s, const wchar_t *fmt, va_list ap) { return vswscanf (s, fmt, ap); }
MSABI void __core_verr (int ev, const char *fmt, va_list ap) { verr (ev, fmt, ap); }
MSABI void __core_verrx (int ev, const char *fmt, va_list ap) { verrx (ev, fmt, ap); }
MSABI void __core_vwarn (const char *fmt, va_list ap) { vwarn (fmt, ap); }
MSABI void __core_vwarnx (const char *fmt, va_list ap) { vwarnx (fmt, ap); }
MSABI void __core_vsyslog (int pri, const char *fmt, va_list ap) { vsyslog (pri, fmt, ap); }

/* The four with no DLL body under any name.
 *
 * vsiprintf: newlib's integer-only vsprintf. The DLL carries the FILE
 * form (vfiprintf) but no string form; the full vsprintf formats every
 * conversion the integer-only one accepts identically, so forwarding to
 * it is a superset, not an approximation. */
MSABI int __core_vsiprintf (char *s, const char *fmt, va_list ap) { return vsprintf (s, fmt, ap); }

/* verror / verror_at_line: Cygwin implements error() over a static
 * _verror, so the v-form has to be written out. Semantics per the
 * glibc-compat contract the DLL's own error() follows: flush stdout,
 * progname prefix (or the caller's hook), the message, the errno text,
 * a count, an exit when status says so. */
static void
core_error_body (int status, int errnum, const char *file, unsigned int line,
                 const char *fmt, va_list ap)
{
  fflush (stdout);
  if (error_print_progname)
    error_print_progname ();
  else
    fprintf (stderr, "%s: ", core_progname);
  if (file)
    fprintf (stderr, "%s:%u: ", file, line);
  vfprintf (stderr, fmt, ap);
  if (errnum)
    fprintf (stderr, ": %s", strerror (errnum));
  fputc ('\n', stderr);
  error_message_count++;
  if (status)
    exit (status);
}

MSABI void
__core_verror (int status, int errnum, const char *fmt, va_list ap)
{
  core_error_body (status, errnum, NULL, 0, fmt, ap);
}

MSABI void
__core_verror_at_line (int status, int errnum, const char *file,
                       unsigned int line, const char *fmt, va_list ap)
{
  static const char *last_file;
  static unsigned int last_line;
  if (error_one_per_line && file && last_file
      && line == last_line && strcmp (file, last_file) == 0)
    return;
  last_file = file;
  last_line = line;
  core_error_body (status, errnum, file, line, fmt, ap);
}

/* vsetproctitle: the BSD surface is variadic only; Cygwin's v-form is
 * static. Format here, then hand the result to the DLL's own
 * setproctitle, keeping the leading-dash convention intact. */
MSABI void
__core_vsetproctitle (const char *fmt, va_list ap)
{
  if (!fmt)
    {
      setproctitle (NULL);
      return;
    }
  char buf[2048];
  vsnprintf (buf, sizeof buf, fmt, ap);
  if (buf[0] == '-')
    setproctitle ("-%s", buf + 1);
  else
    setproctitle ("%s", buf);
}
