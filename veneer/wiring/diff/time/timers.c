/* time slice: process time as invariants -- clock, times, getitimer
   and setitimer -- an armed timer reads back at or under what was
   set, and a disarmed one reads zero. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct itimerval set, got;
    struct tms tms;
    clock_t c, t;
    int rc;

    c = clock();
    printf("clock %d\n", c != (clock_t)-1 && c >= 0);

    t = times(&tms);
    printf("times %d %d\n", t != (clock_t)-1,
           tms.tms_utime >= 0 && tms.tms_stime >= 0 &&
           tms.tms_cutime == 0 && tms.tms_cstime == 0);

    /* Unarmed, the real timer reads all zero. */
    rc = getitimer(ITIMER_REAL, &got);
    printf("unarmed %d %d\n", rc == 0,
           got.it_value.tv_sec == 0 && got.it_value.tv_usec == 0 &&
           got.it_interval.tv_sec == 0 && got.it_interval.tv_usec == 0);

    /* Armed far out, it reads back at or under the set value with the
       interval held exactly. */
    set.it_value.tv_sec = 3600; set.it_value.tv_usec = 0;
    set.it_interval.tv_sec = 60; set.it_interval.tv_usec = 0;
    rc = setitimer(ITIMER_REAL, &set, NULL);
    printf("armed %d\n", rc == 0);
    rc = getitimer(ITIMER_REAL, &got);
    printf("readback %d %d %d\n", rc == 0,
           got.it_value.tv_sec > 3590 && got.it_value.tv_sec <= 3600,
           got.it_interval.tv_sec == 60 && got.it_interval.tv_usec == 0);

    /* Disarming hands back the previous setting. */
    set.it_value.tv_sec = 0; set.it_value.tv_usec = 0;
    set.it_interval.tv_sec = 0; set.it_interval.tv_usec = 0;
    rc = setitimer(ITIMER_REAL, &set, &got);
    printf("disarm %d %d\n", rc == 0, got.it_interval.tv_sec == 60);
    rc = getitimer(ITIMER_REAL, &got);
    printf("off %d %d\n", rc == 0,
           got.it_value.tv_sec == 0 && got.it_value.tv_usec == 0);

    /* A which nobody defines refuses with EINVAL. */
    errno = 0;
    rc = getitimer(1000, &got);
    printf("bad-which %d %d\n", rc == -1, errno == EINVAL);
    errno = 0;
    set.it_value.tv_usec = 2000000; /* out of range */
    set.it_value.tv_sec = 1;
    rc = setitimer(ITIMER_REAL, &set, NULL);
    printf("bad-usec %d %d\n", rc == -1, errno == EINVAL);
    return 0;
}
