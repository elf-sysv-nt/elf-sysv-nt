/* WP-27: the fixed-arity Microsoft-ABI cores behind the nonformat entries.
 *
 * nonformat.c walks each prototype-driven variadic export's trailing
 * arguments and repasses them at fixed arity into a __core_* back end. This
 * unit is that back end, compiled by the host gcc inside the DLL, so each
 * core is a plain call to the DLL's own body under the host's own prototype:
 * the exec and spawn cores land on the v-forms the l-form entries collected
 * an argv for, and the rest hand a variadic body the arguments it would have
 * read itself.
 */
#define _GNU_SOURCE 1
#include <unistd.h>
#include <process.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/cygwin.h>

#define MSABI __attribute__((ms_abi))

MSABI int __core_open (const char *path, int flags, unsigned int mode)
{ return open (path, flags, mode); }

MSABI int __core_openat (int dirfd, const char *path, int flags, unsigned int mode)
{ return openat (dirfd, path, flags, mode); }

MSABI int __core_execv (const char *path, char *const argv[])
{ return execv (path, argv); }

MSABI int __core_execve (const char *path, char *const argv[], char *const envp[])
{ return execve (path, argv, envp); }

MSABI int __core_execvp (const char *file, char *const argv[])
{ return execvp (file, argv); }

MSABI int __core_spawnv (int mode, const char *path, const char *const *argv)
{ return spawnv (mode, path, argv); }

MSABI int __core_spawnve (int mode, const char *path, const char *const *argv,
			  const char *const *envp)
{ return spawnve (mode, path, argv, envp); }

MSABI int __core_spawnvp (int mode, const char *file, const char *const *argv)
{ return spawnvp (mode, file, argv); }

MSABI int __core_fcntl (int fd, int cmd, long arg)
{ return fcntl (fd, cmd, arg); }

MSABI int __core_ioctl (int fd, int cmd, void *arg)
{ return ioctl (fd, cmd, arg); }

MSABI int __core_semctl (int semid, int semnum, int cmd, void *arg)
{ return semctl (semid, semnum, cmd, arg); }

MSABI long __core_mq_open (const char *name, int oflag, unsigned int mode, void *attr)
{ return (long) mq_open (name, oflag, (mode_t) mode, (struct mq_attr *) attr); }

MSABI void *__core_sem_open (const char *name, int oflag, unsigned int mode,
			     unsigned int value)
{ return sem_open (name, oflag, (mode_t) mode, value); }

/* six register-wide slots cover cygwin_internal's widest operation */
MSABI unsigned long __core_cygwin_internal (unsigned int op,
	unsigned long a1, unsigned long a2, unsigned long a3,
	unsigned long a4, unsigned long a5, unsigned long a6)
{ return cygwin_internal ((cygwin_getinfo_types) op, a1, a2, a3, a4, a5, a6); }
