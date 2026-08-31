/* The static sliver of the veneer libc.
 *
 * glibc never exports atexit or at_quick_exit from libc.so.6.  Each is a
 * one-line wrapper that must run in the registering module, because the
 * handle it passes to __cxa_atexit is that module's own __dso_handle --
 * that is how dlclose knows which destructors belong to which library.
 * So glibc ships them in libc_nonshared.a and makes /usr/lib64/libc.so a
 * linker script that groups the shared library with the archive.
 *
 * The veneer must do the same or nothing that says -lc links: el8's own
 * libstdc++.so, built against real glibc, carries an undefined atexit on
 * the promise that the link editor will find it here.  This file is that
 * promise kept.  It is compiled once per sysroot by install-veneer and
 * archived as libc_nonshared.a; the linker script written next to it does
 * the grouping.
 *
 * __dso_handle comes from the compiler's own crtbegin in whichever module
 * is being linked.  The weak reference keeps a bare -nostartfiles link
 * from failing; a module without crtbegin registers against the null
 * handle, which is what glibc does too.
 */

extern void *__dso_handle __attribute__ ((__weak__, __visibility__ ("hidden")));

extern int __cxa_atexit (void (*) (void *), void *, void *);
extern int __cxa_at_quick_exit (void (*) (void *), void *, void *);

int
atexit (void (*func) (void))
{
  return __cxa_atexit ((void (*) (void *)) func, 0,
                       &__dso_handle ? __dso_handle : 0);
}

int
at_quick_exit (void (*func) (void))
{
  return __cxa_at_quick_exit ((void (*) (void *)) func, 0,
                              &__dso_handle ? __dso_handle : 0);
}

/* The stat family lives here too.  el8's glibc 2.28 exports only the
 * versioned entry points -- __xstat, __lxstat, __fxstat, __fxstatat,
 * __xmknod, __xmknodat and their 64 twins -- and supplies stat, lstat,
 * fstat, fstatat, mknod, mknodat as libc_nonshared.a wrappers that bake
 * the caller's idea of struct stat into the first argument.  A program
 * compiled against the el8 headers therefore leaves stat() undefined
 * unless the archive answers, exactly as it does on the real system.
 * The version constants are x86-64's: _STAT_VER_LINUX is 1 and
 * _MKNOD_VER_LINUX is 0, pinned by the ABI these wrappers exist to
 * freeze.  The struct types stay opaque pointers here; the callee only
 * forwards them, so the sliver compiles without dragging sys/stat.h in
 * under any particular _FILE_OFFSET_BITS.
 */

#define ESN_STAT_VER 1
#define ESN_MKNOD_VER 0

typedef unsigned long long esn_dev_t;
typedef unsigned int esn_mode_t;

extern int __xstat (int, const char *, void *);
extern int __xstat64 (int, const char *, void *);
extern int __lxstat (int, const char *, void *);
extern int __lxstat64 (int, const char *, void *);
extern int __fxstat (int, int, void *);
extern int __fxstat64 (int, int, void *);
extern int __fxstatat (int, int, const char *, void *, int);
extern int __fxstatat64 (int, int, const char *, void *, int);
extern int __xmknod (int, const char *, esn_mode_t, esn_dev_t *);
extern int __xmknodat (int, int, const char *, esn_mode_t, esn_dev_t *);

int
stat (const char *path, void *buf)
{
  return __xstat (ESN_STAT_VER, path, buf);
}

int
stat64 (const char *path, void *buf)
{
  return __xstat64 (ESN_STAT_VER, path, buf);
}

int
lstat (const char *path, void *buf)
{
  return __lxstat (ESN_STAT_VER, path, buf);
}

int
lstat64 (const char *path, void *buf)
{
  return __lxstat64 (ESN_STAT_VER, path, buf);
}

int
fstat (int fd, void *buf)
{
  return __fxstat (ESN_STAT_VER, fd, buf);
}

int
fstat64 (int fd, void *buf)
{
  return __fxstat64 (ESN_STAT_VER, fd, buf);
}

int
fstatat (int fd, const char *path, void *buf, int flag)
{
  return __fxstatat (ESN_STAT_VER, fd, path, buf, flag);
}

int
fstatat64 (int fd, const char *path, void *buf, int flag)
{
  return __fxstatat64 (ESN_STAT_VER, fd, path, buf, flag);
}

int
mknod (const char *path, esn_mode_t mode, esn_dev_t dev)
{
  return __xmknod (ESN_MKNOD_VER, path, mode, &dev);
}

int
mknodat (int fd, const char *path, esn_mode_t mode, esn_dev_t dev)
{
  return __xmknodat (ESN_MKNOD_VER, fd, path, mode, &dev);
}
