/*
 * reent-stub-path probe -- WP-56 reent-tls-bringup, item 1.
 *
 * The real-process stub reads the ELF image with CreateFileA (realproc-file.c),
 * which resolves a Windows-form path; the loader is handed a Cygwin POSIX path
 * (loader/exec/t/run.sh runs `-r /bin/echo.exe`). This probe measures the two
 * candidate host-side conversions of that POSIX path into something CreateFileA
 * opens, both without any faced-libc call:
 *
 *   P: the parent's route. A normal host Cygwin process (which the front end
 *      is) asks its own cygwin1.dll -- cygwin_conv_path -- for the Windows form,
 *      then opens it. This is a host cygwin1.dll call, not a faced-runtime one.
 *   S: the stub's own route. The only conversion the real-process stub can make
 *      without crossing into the faced runtime is Win32 GetFullPathNameA, which
 *      knows nothing of Cygwin's mount table.
 *
 * It prints one marker per measurement; measure.sh turns them into findings.
 * The Windows path strings are context, never a finding: they are host- and
 * root-specific. The findings are the yes/no of whether each route's result
 * opens.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/cygwin.h>
#include <stdio.h>

static const char *opens(const char *winpath)
{
HANDLE h = CreateFileA(winpath, GENERIC_READ, FILE_SHARE_READ, NULL,
       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
if (h == INVALID_HANDLE_VALUE)
return "no";
CloseHandle(h);
return "yes";
}

int main(int argc, char **argv)
{
const char *posix = argc > 1 ? argv[1] : "/bin/echo.exe";
char win[MAX_PATH];

printf("I:posix=%s\n", posix);

/* The parent's host cygwin1.dll converts the mount path. */
if (cygwin_conv_path(CCP_POSIX_TO_WIN_A | CCP_ABSOLUTE, posix,
     win, sizeof win) == 0) {
printf("P:conv-win=%s\n", win);
printf("P:conv-opens=%s\n", opens(win));
} else {
printf("P:conv-failed\n");
}

/* The stub's only host-safe conversion. GetFullPathNameA resolves
 * against the current drive and directory, not Cygwin's mounts. */
if (GetFullPathNameA(posix, sizeof win, win, NULL)) {
printf("S:full-win=%s\n", win);
printf("S:full-opens=%s\n", opens(win));
} else {
printf("S:full-failed\n");
}

printf("Z:done\n");
return 0;
}
