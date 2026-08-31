/* io-mux slice: poll and ppoll over the same pipe facts -- no POLLIN
   while empty, POLLIN once written, POLLOUT on the write end, an
   invalid fd reported as POLLNVAL rather than an error return. */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <poll.h>

int main(void)
{
    int p[2];
    struct pollfd fds[2];
    struct timespec ts;
    int n;

    if (pipe(p) != 0) { printf("pipe fail\n"); return 1; }

    fds[0].fd = p[0]; fds[0].events = POLLIN; fds[0].revents = 0;
    n = poll(fds, 1, 0);
    printf("empty %d %d\n", n, (fds[0].revents & POLLIN) != 0);

    if (write(p[1], "x", 1) != 1) { printf("write fail\n"); return 1; }
    fds[0].fd = p[0]; fds[0].events = POLLIN; fds[0].revents = 0;
    fds[1].fd = p[1]; fds[1].events = POLLOUT; fds[1].revents = 0;
    n = poll(fds, 2, 0);
    printf("ready %d %d %d\n", n,
           (fds[0].revents & POLLIN) != 0,
           (fds[1].revents & POLLOUT) != 0);

    fds[0].fd = p[0]; fds[0].events = POLLIN; fds[0].revents = 0;
    ts.tv_sec = 0; ts.tv_nsec = 0;
    n = ppoll(fds, 1, &ts, 0);
    printf("ppoll %d %d\n", n, (fds[0].revents & POLLIN) != 0);

    close(p[0]); close(p[1]);
    fds[0].fd = p[0]; fds[0].events = POLLIN; fds[0].revents = 0;
    n = poll(fds, 1, 0);
    printf("closed %d %d\n", n, (fds[0].revents & POLLNVAL) != 0);
    return 0;
}
