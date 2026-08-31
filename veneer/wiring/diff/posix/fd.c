/* posix slice: descriptors -- pipe, dup family, read, write, lseek,
   pread, pwrite, ftruncate. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    char path[64], buf[32];
    int p[2], fd, d, r;
    ssize_t n;
    off_t o;

    r = pipe(p);
    printf("pipe %d\n", r);
    n = write(p[1], "abc", 3);
    printf("wr %d\n", (int)n);
    n = read(p[0], buf, sizeof buf - 1);
    buf[n < 0 ? 0 : n] = 0;
    printf("rd %d %s\n", (int)n, buf);

    d = dup(p[0]);
    printf("dup %d\n", d >= 0);
    r = dup2(p[1], d);
    printf("dup2 %d\n", r == d);
    r = close(d);
    printf("close %d\n", r);
    r = close(p[0]);
    r += close(p[1]);
    printf("closes %d\n", r);

    snprintf(path, sizeof path, "/tmp/esn-posix-%d.fd", (int)getpid());
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    printf("open %d\n", fd >= 0);
    n = write(fd, "0123456789", 10);
    printf("wr2 %d\n", (int)n);
    o = lseek(fd, 2, SEEK_SET);
    printf("lseek %d\n", (int)o);
    n = read(fd, buf, 3);
    buf[n < 0 ? 0 : n] = 0;
    printf("rd2 %d %s\n", (int)n, buf);
    n = pread(fd, buf, 4, 6);
    buf[n < 0 ? 0 : n] = 0;
    printf("pread %d %s\n", (int)n, buf);
    n = pwrite(fd, "XY", 2, 0);
    printf("pwrite %d\n", (int)n);
    o = lseek(fd, 0, SEEK_CUR);
    printf("cur %d\n", (int)o);
    r = ftruncate(fd, 5);
    printf("ftrunc %d\n", r);
    o = lseek(fd, 0, SEEK_END);
    printf("end %d\n", (int)o);
    n = pread(fd, buf, 5, 0);
    buf[n < 0 ? 0 : n] = 0;
    printf("tail %d %s\n", (int)n, buf);
    r = close(fd);
    r += unlink(path);
    printf("done %d\n", r);
    return 0;
}
