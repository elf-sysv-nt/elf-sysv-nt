/* sockets slice: socket options as round-trips and refusals --
   SO_REUSEADDR, SO_LINGER, SO_KEEPALIVE, the not-connected and
   not-a-name answers, and the absent interface. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <unistd.h>

int main(void)
{
    int fd, v;
    socklen_t len;
    struct linger lg;
    struct sockaddr_in sa;
    char buf[8];

    fd = socket(AF_INET, SOCK_STREAM, 0);
    len = sizeof v;
    printf("reuse-off %d\n",
           getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, &len) == 0 &&
           v == 0);
    v = 1;
    printf("reuse-set %d\n",
           setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof v) == 0);
    v = 0; len = sizeof v;
    printf("reuse-on %d\n",
           getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, &len) == 0 &&
           v != 0);

    lg.l_onoff = 1; lg.l_linger = 5;
    printf("linger-set %d\n",
           setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg) == 0);
    memset(&lg, 0, sizeof lg); len = sizeof lg;
    printf("linger-back %d\n",
           getsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, &len) == 0 &&
           lg.l_onoff != 0 && lg.l_linger == 5);

    v = 1;
    printf("keepalive %d\n",
           setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof v) == 0);

    /* the refusals: not connected, not resolvable, not an interface.
       Each call is sequenced before its printf so the result and the
       errno it set are never read in one unsequenced argument list. */
    {
        int rc;
        errno = 0;
        rc = recv(fd, buf, sizeof buf, 0) == -1;
        printf("notconn %d %d\n", rc, errno == ENOTCONN);
        len = sizeof sa;
        errno = 0;
        rc = getpeername(fd, (struct sockaddr *)&sa, &len) == -1;
        printf("nopeer %d %d\n", rc, errno == ENOTCONN);
        printf("noif %d\n", if_nametoindex("no-such-interface0") == 0);
        errno = 0;
        rc = shutdown(fd, SHUT_RDWR) == -1;
        printf("badshut %d %d\n", rc, errno == ENOTCONN);

        close(fd);
        errno = 0;
        rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof v) == -1;
        printf("closed %d %d\n", rc, errno == EBADF);
    }
    return 0;
}
