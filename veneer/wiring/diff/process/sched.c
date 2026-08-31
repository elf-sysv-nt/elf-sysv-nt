/* process slice: the scheduler surface -- the priority ranges of the
   standard policies, the caller's own policy and parameter, yield, the
   round-robin interval, and the bad-policy refusals. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>

int main(void)
{
    struct sched_param sp;
    struct timespec ts;
    int rc;

    printf("fifo-range %d %d\n",
           sched_get_priority_min(SCHED_FIFO) == 1,
           sched_get_priority_max(SCHED_FIFO) == 99);
    printf("rr-range %d %d\n",
           sched_get_priority_min(SCHED_RR) == 1,
           sched_get_priority_max(SCHED_RR) == 99);
    printf("other-range %d %d\n",
           sched_get_priority_min(SCHED_OTHER) == 0,
           sched_get_priority_max(SCHED_OTHER) == 0);

    errno = 0;
    rc = sched_get_priority_max(1000);
    printf("bad-policy %d %d\n", rc == -1, errno == EINVAL);

    rc = sched_getscheduler(0);
    printf("scheduler %d\n", rc == SCHED_OTHER);

    sp.sched_priority = -1;
    rc = sched_getparam(0, &sp);
    printf("param %d %d\n", rc == 0, sp.sched_priority == 0);

    rc = sched_setscheduler(0, SCHED_OTHER, &sp);
    printf("set-other %d\n", rc == 0);

    sp.sched_priority = 42;
    errno = 0;
    rc = sched_setscheduler(0, SCHED_OTHER, &sp);
    printf("set-bad-prio %d %d\n", rc == -1, errno == EINVAL);

    rc = sched_yield();
    printf("yield %d\n", rc == 0);

    ts.tv_sec = -1;
    rc = sched_rr_get_interval(0, &ts);
    printf("rr-interval %d %d\n", rc == 0, ts.tv_sec == 0);

    printf("getcpu %d\n", sched_getcpu() >= 0);
    return 0;
}
