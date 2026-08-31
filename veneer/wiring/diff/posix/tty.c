/* posix slice: terminals that aren't -- isatty, ttyname, tcgetpgrp on
   a pipe -- plus confstr and the pathconf pair, as invariants. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int main(void)
{
    char buf[256];
    int p[2], fd, r;
    long v;
    size_t n;

    r = pipe(p);
    printf("pipe %d\n", r);
    errno = 0;
    r = isatty(p[0]);
    printf("isatty %d %d\n", r, errno == ENOTTY);
    printf("ttyname %d\n", ttyname(p[0]) == NULL);
    r = ttyname_r(p[0], buf, sizeof buf);
    printf("ttyname_r %d\n", r == ENOTTY);
    printf("tcgetpgrp %d\n", tcgetpgrp(p[0]) == -1);
    r = close(p[0]);
    r += close(p[1]);
    printf("closed %d\n", r);

    n = confstr(_CS_PATH, buf, sizeof buf);
    printf("confstr %d %d\n", n > 0, buf[0] == '/');

    v = pathconf("/tmp", _PC_NAME_MAX);
    printf("name-max %d\n", v > 0);
    fd = open("/tmp", O_RDONLY);
    v = fpathconf(fd, _PC_LINK_MAX);
    r = close(fd);
    printf("link-max %d %d\n", v > 0, r);

    errno = 0;
    v = pathconf("/tmp/definitely-not-a-real-dir-esn", _PC_NAME_MAX);
    printf("bad %d %d\n", v == -1, errno == ENOENT);
    return 0;
}
