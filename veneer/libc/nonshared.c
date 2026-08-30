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
