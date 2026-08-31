/* io-mux slice: the timerfd trio -- create on the monotonic clock, arm
   a one-shot, see gettime report it armed, block in read for exactly
   one expiration, and disarm back to zero. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/timerfd.h>

int main(void)
{
    struct itimerspec its, cur;
    uint64_t exp;
    int fd;
    ssize_t n;

    fd = timerfd_create(CLOCK_MONOTONIC, 0);
    printf("open %d\n", fd >= 0);

    memset(&its, 0, sizeof its);
    its.it_value.tv_nsec = 50 * 1000 * 1000;   /* 50ms, one shot */
    printf("arm %d\n", timerfd_settime(fd, 0, &its, 0) == 0);

    printf("gettime %d\n", timerfd_gettime(fd, &cur) == 0 &&
           (cur.it_value.tv_sec > 0 || cur.it_value.tv_nsec > 0));

    exp = 0;
    n = read(fd, &exp, sizeof exp);
    printf("expire %d %d\n", n == (ssize_t)sizeof exp, exp == 1);

    memset(&its, 0, sizeof its);
    printf("disarm %d\n", timerfd_settime(fd, 0, &its, 0) == 0 &&
           timerfd_gettime(fd, &cur) == 0 &&
           cur.it_value.tv_sec == 0 && cur.it_value.tv_nsec == 0);

    close(fd);
    return 0;
}
