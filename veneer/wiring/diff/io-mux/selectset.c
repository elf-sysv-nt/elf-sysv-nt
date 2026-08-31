/* io-mux slice: select and pselect over a pipe -- an empty pipe is not
   readable under a zero timeout, becomes readable once written, the
   write end is writable throughout, and a bad fd is EBADF. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>

int main(void)
{
    int p[2];
    fd_set rs, ws;
    struct timeval tv;
    struct timespec ts;
    int n;

    if (pipe(p) != 0) { printf("pipe fail\n"); return 1; }

    FD_ZERO(&rs); FD_SET(p[0], &rs);
    tv.tv_sec = 0; tv.tv_usec = 0;
    n = select(p[0] + 1, &rs, 0, 0, &tv);
    printf("empty %d %d\n", n, FD_ISSET(p[0], &rs) != 0);

    if (write(p[1], "x", 1) != 1) { printf("write fail\n"); return 1; }
    FD_ZERO(&rs); FD_SET(p[0], &rs);
    FD_ZERO(&ws); FD_SET(p[1], &ws);
    tv.tv_sec = 0; tv.tv_usec = 0;
    n = select((p[0] > p[1] ? p[0] : p[1]) + 1, &rs, &ws, 0, &tv);
    printf("ready %d %d %d\n", n,
           FD_ISSET(p[0], &rs) != 0, FD_ISSET(p[1], &ws) != 0);

    FD_ZERO(&rs); FD_SET(p[0], &rs);
    ts.tv_sec = 0; ts.tv_nsec = 0;
    n = pselect(p[0] + 1, &rs, 0, 0, &ts, 0);
    printf("pselect %d %d\n", n, FD_ISSET(p[0], &rs) != 0);

    close(p[0]); close(p[1]);
    FD_ZERO(&rs); FD_SET(p[0], &rs);
    tv.tv_sec = 0; tv.tv_usec = 0;
    errno = 0;
    n = select(p[0] + 1, &rs, 0, 0, &tv);
    printf("badfd %d %d\n", n, errno == EBADF);
    return 0;
}
