/* threads slice: the attribute object's defaults and round-trips. A
   fresh pthread_attr_t answers the documented defaults through every
   getter this slice wires; each setter's stored value comes back
   through its getter; the out-of-range detach state is refused EINVAL
   and the process contention scope ENOTSUP; and destroy returns 0. */
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>

int main(void)
{
    pthread_attr_t a;
    struct sched_param sp;
    int v, r;

    printf("init %d\n", pthread_attr_init(&a));

    pthread_attr_getdetachstate(&a, &v);
    printf("default detachstate joinable %d\n", v == PTHREAD_CREATE_JOINABLE);
    pthread_attr_getinheritsched(&a, &v);
    printf("default inheritsched inherit %d\n", v == PTHREAD_INHERIT_SCHED);
    pthread_attr_getschedpolicy(&a, &v);
    printf("default schedpolicy other %d\n", v == SCHED_OTHER);
    pthread_attr_getscope(&a, &v);
    printf("default scope system %d\n", v == PTHREAD_SCOPE_SYSTEM);
    pthread_attr_getschedparam(&a, &sp);
    printf("default schedparam prio %d\n", sp.sched_priority);

    r = pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_attr_getdetachstate(&a, &v);
    printf("detached set %d stored %d\n", r, v == PTHREAD_CREATE_DETACHED);
    r = pthread_attr_setdetachstate(&a, 12345);
    printf("bad detachstate EINVAL %d\n", r == EINVAL);

    r = pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_getinheritsched(&a, &v);
    printf("explicit set %d stored %d\n", r, v == PTHREAD_EXPLICIT_SCHED);

    r = pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    pthread_attr_getschedpolicy(&a, &v);
    printf("fifo set %d stored %d\n", r, v == SCHED_FIFO);

    sp.sched_priority = 5;
    r = pthread_attr_setschedparam(&a, &sp);
    sp.sched_priority = -1;
    pthread_attr_getschedparam(&a, &sp);
    printf("schedparam set %d stored %d\n", r, sp.sched_priority == 5);

    r = pthread_attr_setscope(&a, PTHREAD_SCOPE_PROCESS);
    printf("process scope ENOTSUP %d\n", r == ENOTSUP);
    r = pthread_attr_setscope(&a, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_getscope(&a, &v);
    printf("system scope set %d stored %d\n", r, v == PTHREAD_SCOPE_SYSTEM);

    printf("destroy %d\n", pthread_attr_destroy(&a));
    return 0;
}
