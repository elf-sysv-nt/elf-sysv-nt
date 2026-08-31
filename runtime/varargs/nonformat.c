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
 * different and there is no family to fold them into. All fourteen are here;
 * open and execl came first and set the pattern the rest follow. This unit
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

typedef long __nf_mqd_t;		/* mqd_t is intptr_t on this target */

/* the fixed-arity Microsoft-ABI cores (WP-22's to provide) */
__attribute__((ms_abi)) int  __core_open(const char *path, int flags, __nf_mode_t mode);
__attribute__((ms_abi)) int  __core_openat(int dirfd, const char *path, int flags, __nf_mode_t mode);
__attribute__((ms_abi)) int  __core_execv(const char *path, char *const argv[]);
__attribute__((ms_abi)) int  __core_execve(const char *path, char *const argv[], char *const envp[]);
__attribute__((ms_abi)) int  __core_execvp(const char *file, char *const argv[]);
__attribute__((ms_abi)) int  __core_spawnv(int mode, const char *path, const char *const *argv);
__attribute__((ms_abi)) int  __core_spawnve(int mode, const char *path, const char *const *argv,
					    const char *const *envp);
__attribute__((ms_abi)) int  __core_spawnvp(int mode, const char *file, const char *const *argv);
__attribute__((ms_abi)) int  __core_fcntl(int fd, int cmd, long arg);
__attribute__((ms_abi)) int  __core_ioctl(int fd, int cmd, void *arg);
__attribute__((ms_abi)) int  __core_semctl(int semid, int semnum, int cmd, void *arg);
__attribute__((ms_abi)) __nf_mqd_t __core_mq_open(const char *name, int oflag,
						  __nf_mode_t mode, void *attr);
__attribute__((ms_abi)) void *__core_sem_open(const char *name, int oflag,
					      __nf_mode_t mode, unsigned int value);
__attribute__((ms_abi)) unsigned long __core_cygwin_internal(unsigned int op,
	unsigned long a1, unsigned long a2, unsigned long a3,
	unsigned long a4, unsigned long a5, unsigned long a6);

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
	int n = 1;
	__sysv_va_list ap;

	argv[0] = (char *)arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, char *);
	argv[n] = NULL;
	__sysv_va_end(ap);
	return __core_execv(path, argv);
}

/*
 * execle appends one argument past the terminator, the environment, which is
 * why the walk must stop reading at the first NULL rather than after it (a
 * lone NULL arg0 is legal and leaves envp the next thing in the list).
 * execlp changes only the core, which searches PATH.
 */
__attribute__((sysv_abi))
int execle(const char *path, const char *arg0, ...)
{
	char *argv[NF_ARGV_MAX];
	char *const *envp;
	int n = 1;
	__sysv_va_list ap;

	argv[0] = (char *)arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, char *);
	argv[n] = NULL;
	envp = __sysv_va_arg(ap, char *const *);
	__sysv_va_end(ap);
	return __core_execve(path, argv, envp);
}

__attribute__((sysv_abi))
int execlp(const char *file, const char *arg0, ...)
{
	char *argv[NF_ARGV_MAX];
	int n = 1;
	__sysv_va_list ap;

	argv[0] = (char *)arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, char *);
	argv[n] = NULL;
	__sysv_va_end(ap);
	return __core_execvp(file, argv);
}

/* the spawn family is Cygwin's own: execl's walk behind a leading mode */
__attribute__((sysv_abi))
int spawnl(int mode, const char *path, const char *arg0, ...)
{
	const char *argv[NF_ARGV_MAX];
	int n = 1;
	__sysv_va_list ap;

	argv[0] = arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, const char *);
	argv[n] = NULL;
	__sysv_va_end(ap);
	return __core_spawnv(mode, path, argv);
}

__attribute__((sysv_abi))
int spawnle(int mode, const char *path, const char *arg0, ...)
{
	const char *argv[NF_ARGV_MAX];
	const char *const *envp;
	int n = 1;
	__sysv_va_list ap;

	argv[0] = arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, const char *);
	argv[n] = NULL;
	envp = __sysv_va_arg(ap, const char *const *);
	__sysv_va_end(ap);
	return __core_spawnve(mode, path, argv, envp);
}

__attribute__((sysv_abi))
int spawnlp(int mode, const char *file, const char *arg0, ...)
{
	const char *argv[NF_ARGV_MAX];
	int n = 1;
	__sysv_va_list ap;

	argv[0] = arg0;
	__sysv_va_start(ap, arg0);
	while (argv[n - 1] != NULL && n < NF_ARGV_MAX - 1)
		argv[n++] = __sysv_va_arg(ap, const char *);
	argv[n] = NULL;
	__sysv_va_end(ap);
	return __core_spawnvp(mode, file, argv);
}

/* openat is open with a directory in front: the same always-read mode */
__attribute__((sysv_abi))
int openat(int dirfd, const char *path, int flags, ...)
{
	__nf_mode_t mode;
	__sysv_va_list ap;

	__sysv_va_start(ap, flags);
	mode = (__nf_mode_t)__sysv_va_arg(ap, int);
	__sysv_va_end(ap);
	return __core_openat(dirfd, path, flags, mode);
}

/*
 * fcntl, ioctl, and semctl each carry at most one trailing argument whose
 * kind the command code decides -- an int for one command, a structure
 * pointer for another. Both ABIs hand the first trailing argument over in a
 * single integer register, so one pointer-wide read moves it whichever kind
 * it was, and a command that takes nothing makes the read a dead load the
 * core never looks at.
 */
__attribute__((sysv_abi))
int fcntl(int fd, int cmd, ...)
{
	long arg;
	__sysv_va_list ap;

	__sysv_va_start(ap, cmd);
	arg = __sysv_va_arg(ap, long);
	__sysv_va_end(ap);
	return __core_fcntl(fd, cmd, arg);
}

__attribute__((sysv_abi))
int ioctl(int fd, int cmd, ...)
{
	void *arg;
	__sysv_va_list ap;

	__sysv_va_start(ap, cmd);
	arg = __sysv_va_arg(ap, void *);
	__sysv_va_end(ap);
	return __core_ioctl(fd, cmd, arg);
}

__attribute__((sysv_abi))
int semctl(int semid, int semnum, int cmd, ...)
{
	void *arg;	/* union semun is one register wide on this target */
	__sysv_va_list ap;

	__sysv_va_start(ap, cmd);
	arg = __sysv_va_arg(ap, void *);
	__sysv_va_end(ap);
	return __core_semctl(semid, semnum, cmd, arg);
}

/*
 * mq_open and sem_open take their trailing pair -- a mode, then an attribute
 * pointer or an initial count -- only under O_CREAT. As with open, the entry
 * cannot know that from here, so it reads both unconditionally and the core
 * ignores what was never passed.
 */
__attribute__((sysv_abi))
__nf_mqd_t mq_open(const char *name, int oflag, ...)
{
	__nf_mode_t mode;
	void *attr;
	__sysv_va_list ap;

	__sysv_va_start(ap, oflag);
	mode = (__nf_mode_t)__sysv_va_arg(ap, int);
	attr = __sysv_va_arg(ap, void *);
	__sysv_va_end(ap);
	return __core_mq_open(name, oflag, mode, attr);
}

__attribute__((sysv_abi))
void *sem_open(const char *name, int oflag, ...)
{
	__nf_mode_t mode;
	unsigned int value;
	__sysv_va_list ap;

	__sysv_va_start(ap, oflag);
	mode = (__nf_mode_t)__sysv_va_arg(ap, int);
	value = __sysv_va_arg(ap, unsigned int);
	__sysv_va_end(ap);
	return __core_sem_open(name, oflag, mode, value);
}

/*
 * cygwin_internal has no one prototype at all; the operation code selects
 * among dozens of shapes. None carries more than six trailing arguments and
 * every argument is register-wide, so six unconditional reads cover the
 * widest shape and the operation uses what it names.
 */
__attribute__((sysv_abi))
unsigned long cygwin_internal(unsigned int op, ...)
{
	unsigned long a[6];
	int i;
	__sysv_va_list ap;

	__sysv_va_start(ap, op);
	for (i = 0; i < 6; i++)
		a[i] = __sysv_va_arg(ap, unsigned long);
	__sysv_va_end(ap);
	return __core_cygwin_internal(op, a[0], a[1], a[2], a[3], a[4], a[5]);
}
