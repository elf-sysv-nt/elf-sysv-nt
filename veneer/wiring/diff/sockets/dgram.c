/* sockets slice: datagrams over loopback -- sendto/recvfrom with the
   sender identified, a connected UDP socket, and the empty-socket
   answer under MSG_DONTWAIT. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static int bound(struct sockaddr_in *sa)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    socklen_t len = sizeof *sa;
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *)sa, sizeof *sa) != 0 ||
        getsockname(fd, (struct sockaddr *)sa, &len) != 0)
        return -1;
    return fd;
}

int main(void)
{
    struct sockaddr_in a, b, from;
    int fa, fb;
    socklen_t len;
    char buf[32];
    ssize_t n;

    fa = bound(&a);
    fb = bound(&b);
    printf("bound %d\n", fa >= 0 && fb >= 0 && a.sin_port != b.sin_port);

    n = sendto(fa, "gram", 4, 0, (struct sockaddr *)&b, sizeof b);
    printf("sendto %d\n", n == 4);
    len = sizeof from;
    n = recvfrom(fb, buf, sizeof buf, 0, (struct sockaddr *)&from, &len);
    printf("recvfrom %d\n", n == 4 && memcmp(buf, "gram", 4) == 0);
    printf("sender %d\n", from.sin_port == a.sin_port &&
           from.sin_addr.s_addr == a.sin_addr.s_addr);

    /* datagram boundaries hold: two sends are two receives */
    sendto(fa, "one", 3, 0, (struct sockaddr *)&b, sizeof b);
    sendto(fa, "two", 3, 0, (struct sockaddr *)&b, sizeof b);
    n = recv(fb, buf, sizeof buf, 0);
    printf("gram1 %d\n", n == 3 && memcmp(buf, "one", 3) == 0);
    n = recv(fb, buf, sizeof buf, 0);
    printf("gram2 %d\n", n == 3 && memcmp(buf, "two", 3) == 0);

    /* a connected datagram socket may just send */
    printf("connect %d\n",
           connect(fb, (struct sockaddr *)&a, sizeof a) == 0);
    printf("send %d\n", send(fb, "back", 4, 0) == 4);
    n = recv(fa, buf, sizeof buf, 0);
    printf("recv %d\n", n == 4 && memcmp(buf, "back", 4) == 0);

    /* and an empty one answers EAGAIN rather than blocking */
    errno = 0;
    n = recv(fb, buf, sizeof buf, MSG_DONTWAIT);
    printf("dontwait %d %d\n", n == -1, errno == EAGAIN);

    close(fa);
    close(fb);
    return 0;
}
