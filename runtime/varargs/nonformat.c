/* WP-24: the prototype-driven variadic entry points.
 *
 * Fourteen exports are variadic but carry no format string: the exec and spawn
 * families, open and openat, fcntl and ioctl, the SysV IPC ctls, mq_open and
 * sem_open, and cygwin_internal. Nothing generic drives their unpack the way a
 * format drives printf's, but nothing has to: each has a fixed, known
 * signature, so its trailing arguments are walked one at a time with
 * __sysv_va_arg for the exact types the prototype names and repassed to a
 * fixed-arity Microsoft-ABI core. There is no va_list rebuild here -- the core
 * takes ordinary arguments, and the System V to Microsoft shuffle is the
 * compiler's at the call site, as in WP-21.
 *
 * They are hand-written rather than generated because each signature is
 * different and there is no family to fold them into. Two are worked here as
 * the pattern for the rest; the enumeration lists all fourteen. This unit
 * defines the real export names, so it is compiled against the runtime's own
 * System V-faced headers, never the host's (whose open and execl are Microsoft
 * ABI); the reproduce test compiles it to an object under minimal local
 * declarations to keep that clean.
 *
 * The cores named here are the Microsoft-ABI back ends WP-22 provides, the same
 * contract shape as core.h. They are declared, not defined, in this package.
 */
#include "sv2ms.h"

#include <stddef.h>

typedef unsigned int __nf_mode_t;	/* mode_t is int-width on this target */

/* the fixed-arity Microsoft-ABI cores (WP-22's to provide) */
__attribute__((ms_abi)) int  __core_open(const char *path, int flags, __nf_mode_t mode);
__attribute__((ms_abi)) int  __core_execv(const char *path, char *const argv[]);

/*
 * open(path, flags, ...) -- the mode argument is present only when flags
 * carries O_CREAT (or O_TMPFILE), but a variadic callee cannot see flags'
 * meaning, so the entry always makes room for one int mode and passes it on;
 * when it was not supplied the core ignores it, exactly as the host's open
 * does. One trailing argument, one known type: the whole unpack.
 */
__attribute__((sysv_abi))
int open(const char *path, int flags, ...)
{
	__nf_mode_t mode = 0;
	__sysv_va_list ap;

	__sysv_va_start(ap, flags);
	mode = (__nf_mode_t)__sysv_va_arg(ap, int);
	__sysv_va_end(ap);
	return __core_open(path, flags, mode);
}

/*
 * execl(path, arg0, ..., NULL) -- a NULL-terminated list of char*, collected
 * into the argv array execv wants. The bound is the reason this cannot be a
 * generic forward and also the reason it is simple: the list ends at the first
 * NULL, and every element is one pointer. execle's trailing envp and execlp's
 * path search are the same walk with one more step, which is why the family
 * shares this shape.
 */
#define NF_ARGV_MAX 1024

__attribute__((sysv_abi))
int execl(const char *path, const char *arg0, ...)
{
	char *argv[NF_ARGV_MAX];
	int n = 0;
	__sysv_va_list ap;

	argv[n++] = (char *)arg0;
	__sysv_va_start(ap, arg0);
	while (n < NF_ARGV_MAX - 1) {
		char *a = __sysv_va_arg(ap, char *);
		argv[n++] = a;
		if (a == NULL)
			break;
	}
	argv[n] = NULL;
	__sysv_va_end(ap);
	return __core_execv(path, argv);
}
