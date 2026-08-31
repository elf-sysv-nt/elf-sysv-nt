/* time slice: the posix clocks as invariants -- clock_gettime,
   clock_getres, clock_getcpuclockid, timespec_get, nanosleep and
   clock_nanosleep -- never as raw readings. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct timespec a, b, res, req;
    clockid_t cid;
    int rc;

    rc = clock_gettime(CLOCK_REALTIME, &a);
    printf("real %d %d %d\n", rc == 0, a.tv_sec > 1500000000,
           a.tv_nsec >= 0 && a.tv_nsec < 1000000000);

    rc = clock_gettime(CLOCK_MONOTONIC, &a);
    printf("mono %d\n", rc == 0);
    rc = clock_gettime(CLOCK_MONOTONIC, &b);
    printf("mono-order %d %d\n", rc == 0,
           b.tv_sec > a.tv_sec ||
           (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));

    rc = clock_getres(CLOCK_REALTIME, &res);
    printf("res %d %d\n", rc == 0,
           res.tv_sec == 0 && res.tv_nsec > 0);

    errno = 0;
    rc = clock_gettime((clockid_t)-1, &a);
    printf("bad-clock %d %d\n", rc == -1, errno == EINVAL);

    rc = clock_getcpuclockid(0, &cid);
    printf("cpuclock %d\n", rc == 0);
    rc = clock_gettime(cid, &a);
    printf("cpuclock-read %d\n", rc == 0);

    rc = timespec_get(&a, TIME_UTC);
    printf("tsget %d %d\n", rc == TIME_UTC, a.tv_sec > 1500000000);

    req.tv_sec = 0; req.tv_nsec = 1000000;
    printf("nanosleep %d\n", nanosleep(&req, NULL) == 0);
    errno = 0;
    req.tv_nsec = 1000000000;
    rc = nanosleep(&req, NULL);
    printf("nanosleep-bad %d %d\n", rc == -1, errno == EINVAL);

    req.tv_sec = 0; req.tv_nsec = 1000000;
    rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    printf("clock-nanosleep %d\n", rc == 0);
    req.tv_nsec = -1;
    rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    printf("clock-nanosleep-bad %d\n", rc == EINVAL);
    return 0;
}
