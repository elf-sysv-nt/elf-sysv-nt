/* sockets slice: a loopback TCP conversation -- socket, bind to an
   ephemeral port, listen, connect, accept4, the name calls agreeing
   on both ends, data both ways, sockatmark off-band. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(void)
{
    int ls, cs, as;
    struct sockaddr_in sa, peer, mine;
    socklen_t len;
    char buf[32];
    int v;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    printf("socket %d\n", ls >= 0);
    len = sizeof v;
    printf("so-type %d\n",
           getsockopt(ls, SOL_SOCKET, SO_TYPE, &v, &len) == 0 &&
           v == SOCK_STREAM);

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    printf("bind %d\n", bind(ls, (struct sockaddr *)&sa, sizeof sa) == 0);
    len = sizeof sa;
    printf("getsockname %d\n",
           getsockname(ls, (struct sockaddr *)&sa, &len) == 0 &&
           sa.sin_family == AF_INET && sa.sin_port != 0);
    printf("listen %d\n", listen(ls, 1) == 0);

    cs = socket(AF_INET, SOCK_STREAM, 0);
    printf("connect %d\n",
           connect(cs, (struct sockaddr *)&sa, sizeof sa) == 0);
    len = sizeof peer;
    as = accept4(ls, (struct sockaddr *)&peer, &len, SOCK_CLOEXEC);
    printf("accept4 %d\n", as >= 0);
    printf("cloexec %d\n", (fcntl(as, F_GETFD) & FD_CLOEXEC) != 0);

    /* both ends agree on who is who */
    len = sizeof mine;
    printf("peer-is-me %d\n",
           getsockname(cs, (struct sockaddr *)&mine, &len) == 0 &&
           mine.sin_port == peer.sin_port &&
           mine.sin_addr.s_addr == peer.sin_addr.s_addr);
    len = sizeof peer;
    printf("me-is-server %d\n",
           getpeername(cs, (struct sockaddr *)&peer, &len) == 0 &&
           peer.sin_port == sa.sin_port);

    printf("send %d\n", send(cs, "ping", 4, 0) == 4);
    printf("recv %d\n", recv(as, buf, sizeof buf, 0) == 4 &&
           memcmp(buf, "ping", 4) == 0);
    printf("reply %d\n", send(as, "pong!", 5, 0) == 5);
    printf("heard %d\n", recv(cs, buf, sizeof buf, 0) == 5 &&
           memcmp(buf, "pong!", 5) == 0);

    printf("atmark %d\n", sockatmark(cs) == 0);
    len = sizeof v;
    printf("so-error %d\n",
           getsockopt(cs, SOL_SOCKET, SO_ERROR, &v, &len) == 0 && v == 0);

    close(cs);
    close(as);
    close(ls);
    return 0;
}
