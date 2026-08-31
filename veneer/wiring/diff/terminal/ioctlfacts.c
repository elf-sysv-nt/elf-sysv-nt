/* terminal slice: ioctl, the slice's one shim, observed over a pipe --
   FIONREAD counts what sits unread, FIONBIO's edit shows up in the
   fcntl flags, FIOCLEX sets close-on-exec, a terminal request on a
   pipe answers ENOTTY, and a closed descriptor answers EBADF. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

int main(void)
{
    struct winsize ws;
    int p[2];
    int n, r, on, fl;

    if (pipe(p) != 0) { printf("pipe fail\n"); return 1; }

    n = -1;
    r = ioctl(p[0], FIONREAD, &n);
    printf("empty %d %d\n", r, n);

    if (write(p[1], "abc", 3) != 3) { printf("write fail\n"); return 1; }
    n = -1;
    r = ioctl(p[0], FIONREAD, &n);
    printf("filled %d %d\n", r, n);

    on = 1;
    r = ioctl(p[0], FIONBIO, &on);
    fl = fcntl(p[0], F_GETFL);
    printf("nbio %d %d\n", r, (fl & O_NONBLOCK) != 0);
    on = 0;
    r = ioctl(p[0], FIONBIO, &on);
    fl = fcntl(p[0], F_GETFL);
    printf("nbioff %d %d\n", r, (fl & O_NONBLOCK) == 0);

    r = ioctl(p[0], FIOCLEX);
    fl = fcntl(p[0], F_GETFD);
    printf("clex %d %d\n", r, (fl & FD_CLOEXEC) != 0);

    errno = 0;
    r = ioctl(p[0], TIOCGWINSZ, &ws);
    printf("notatty %d %d\n", r, errno == ENOTTY);

    close(p[0]); close(p[1]);
    errno = 0;
    n = 0;
    r = ioctl(p[0], FIONREAD, &n);
    printf("gone %d %d\n", r, errno == EBADF);
    return 0;
}
