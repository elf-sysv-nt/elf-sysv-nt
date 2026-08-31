/* io slice: readv and writev over a pipe -- the two rows this slice
   wires, aio, sendfile and the preadv/pwritev family staying stub for
   now.  writev gathers three buffers into one write, readv scatters
   the bytes back across buffers whose sizes do not line up with the
   writer's, and a zero-length iovec entry contributes nothing. */
#define _GNU_SOURCE
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    int fds[2];
    pipe(fds);

    char a[] = "abc", b[] = "", c[] = "defgh";
    struct iovec wv[3] = {
        { a, strlen(a) },
        { b, strlen(b) },
        { c, strlen(c) },
    };
    ssize_t n = writev(fds[1], wv, 3);
    printf("writev n %zd\n", n);

    char p1[2] = {0}, p2[3] = {0}, p3[10] = {0};
    struct iovec rv[3] = {
        { p1, sizeof p1 },
        { p2, sizeof p2 },
        { p3, sizeof p3 },
    };
    ssize_t m = readv(fds[0], rv, 3);
    printf("readv n %zd\n", m);
    printf("part1 %.2s\n", p1);
    printf("part2 %.3s\n", p2);
    printf("part3 %.3s\n", p3);

    close(fds[1]);
    close(fds[0]);
    return 0;
}
