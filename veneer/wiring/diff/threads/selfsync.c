/* threads slice: identity, the mutex protocol, and the cancellation
   switches, all observable from the one thread there is. pthread_self
   agrees with itself through pthread_equal and a copy; a mutex inits,
   locks, unlocks and destroys with 0 at every step, and the static
   initializer's mutex does the same; pthread_getschedparam on self
   answers SCHED_OTHER at priority 0 and setting the same back is
   accepted; each cancellation switch hands back the state it
   replaces and refuses a made-up value EINVAL. */
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>

static pthread_mutex_t stm = PTHREAD_MUTEX_INITIALIZER;

int main(void)
{
    pthread_t me = pthread_self(), copy = me;
    pthread_mutex_t m;
    struct sched_param sp;
    int pol, old, r;

    printf("self equals self %d\n", pthread_equal(pthread_self(), me) != 0);
    printf("copy equals self %d\n", pthread_equal(copy, me) != 0);

    printf("mutex init %d\n", pthread_mutex_init(&m, NULL));
    printf("mutex lock %d\n", pthread_mutex_lock(&m));
    printf("mutex unlock %d\n", pthread_mutex_unlock(&m));
    printf("mutex destroy %d\n", pthread_mutex_destroy(&m));

    printf("static lock %d\n", pthread_mutex_lock(&stm));
    printf("static unlock %d\n", pthread_mutex_unlock(&stm));

    r = pthread_getschedparam(me, &pol, &sp);
    printf("getschedparam %d other %d prio %d\n",
           r, pol == SCHED_OTHER, sp.sched_priority);
    r = pthread_setschedparam(me, pol, &sp);
    printf("setschedparam same back %d\n", r);

    r = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
    printf("disable %d was enable %d\n", r, old == PTHREAD_CANCEL_ENABLE);
    r = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old);
    printf("enable %d was disable %d\n", r, old == PTHREAD_CANCEL_DISABLE);
    r = pthread_setcancelstate(9999, &old);
    printf("bad state EINVAL %d\n", r == EINVAL);

    r = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old);
    printf("async %d was deferred %d\n", r, old == PTHREAD_CANCEL_DEFERRED);
    r = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old);
    printf("deferred %d was async %d\n", r, old == PTHREAD_CANCEL_ASYNCHRONOUS);
    r = pthread_setcanceltype(9999, &old);
    printf("bad type EINVAL %d\n", r == EINVAL);
    return 0;
}
