/* posix slice: names -- link, symlink, readlink, access, truncate,
   unlink, and the cwd pair. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    char a[64], b[64], l[64], buf[128];
    int fd, r;
    ssize_t n;

    snprintf(a, sizeof a, "/tmp/esn-posix-%d.a", (int)getpid());
    snprintf(b, sizeof b, "/tmp/esn-posix-%d.b", (int)getpid());
    snprintf(l, sizeof l, "/tmp/esn-posix-%d.l", (int)getpid());

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    n = write(fd, "content\n", 8);
    r = close(fd);
    printf("mk %d %d %d\n", fd >= 0, (int)n, r);

    r = access(a, R_OK | W_OK);
    printf("access %d\n", r);
    r = access(b, F_OK);
    printf("absent %d\n", r == -1);

    r = link(a, b);
    printf("link %d\n", r);
    r = symlink(a, l);
    printf("symlink %d\n", r);
    n = readlink(l, buf, sizeof buf - 1);
    buf[n < 0 ? 0 : n] = 0;
    printf("readlink %d\n", n == (ssize_t)strlen(a) && strcmp(buf, a) == 0);

    r = truncate(a, 3);
    printf("trunc %d\n", r);
    fd = open(l, O_RDONLY);
    n = read(fd, buf, sizeof buf - 1);
    buf[n < 0 ? 0 : n] = 0;
    r = close(fd);
    printf("via-link %d %s %d\n", (int)n, buf, r);

    r = unlink(a);
    printf("unlink %d\n", r);
    r = access(b, F_OK);
    printf("hard-left %d\n", r);
    fd = open(l, O_RDONLY);
    printf("dangling %d\n", fd == -1);
    r = unlink(b);
    r += unlink(l);
    printf("clean %d\n", r);
    return 0;
}
