/* sockets slice: socketpair and the message calls -- send, recv with
   MSG_PEEK, sendmsg/recvmsg over iovecs, shutdown read as EOF and
   written as EPIPE. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
    int sv[2];
    char buf[64];
    ssize_t n;

    signal(SIGPIPE, SIG_IGN);
    printf("pair %d\n", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    n = send(sv[0], "hello", 5, 0);
    printf("send %d\n", n == 5);
    n = recv(sv[1], buf, sizeof buf, MSG_PEEK);
    printf("peek %d\n", n == 5 && memcmp(buf, "hello", 5) == 0);
    n = recv(sv[1], buf, sizeof buf, 0);
    printf("recv %d\n", n == 5 && memcmp(buf, "hello", 5) == 0);

    /* the vectored pair: two iovecs out, one in, order preserved */
    {
        struct iovec iov[2];
        struct msghdr mh;
        iov[0].iov_base = "abc"; iov[0].iov_len = 3;
        iov[1].iov_base = "defg"; iov[1].iov_len = 4;
        memset(&mh, 0, sizeof mh);
        mh.msg_iov = iov; mh.msg_iovlen = 2;
        n = sendmsg(sv[0], &mh, 0);
        printf("sendmsg %d\n", n == 7);
        iov[0].iov_base = buf; iov[0].iov_len = sizeof buf;
        memset(&mh, 0, sizeof mh);
        mh.msg_iov = iov; mh.msg_iovlen = 1;
        n = recvmsg(sv[1], &mh, 0);
        printf("recvmsg %d\n", n == 7 && memcmp(buf, "abcdefg", 7) == 0);
    }

    /* half-close: the peer reads EOF, the closer writes EPIPE */
    printf("shutdown %d\n", shutdown(sv[0], SHUT_WR) == 0);
    n = recv(sv[1], buf, sizeof buf, 0);
    printf("eof %d\n", n == 0);
    errno = 0;
    n = send(sv[0], "x", 1, 0);
    printf("epipe %d %d\n", n == -1, errno == EPIPE);

    /* the peer can still talk the other way */
    n = send(sv[1], "back", 4, 0);
    printf("back %d\n", n == 4);
    n = recv(sv[0], buf, sizeof buf, 0);
    printf("heard %d\n", n == 4 && memcmp(buf, "back", 4) == 0);

    close(sv[0]);
    close(sv[1]);
    return 0;
}
