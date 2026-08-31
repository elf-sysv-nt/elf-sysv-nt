/* threads slice: the condition variable's waiter-free protocol. With
   no waiter to find, init, signal, broadcast and destroy all answer
   0, for a fresh object and for the static initializer alike; a
   timedwait whose absolute deadline already passed hands the mutex
   back with ETIMEDOUT — observed by relocking after — and the
   condattr object inits and destroys clean. These are the versioned
   GLIBC_2.3.2 entries, the slice's doubled rows. */
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static pthread_cond_t stc = PTHREAD_COND_INITIALIZER;

int main(void)
{
    pthread_cond_t c;
    pthread_condattr_t ca;
    pthread_mutex_t m;
    struct timespec past;
    int r;

    printf("condattr init %d destroy %d\n",
           pthread_condattr_init(&ca), pthread_condattr_destroy(&ca));

    printf("cond init %d\n", pthread_cond_init(&c, NULL));
    printf("signal no waiter %d\n", pthread_cond_signal(&c));
    printf("broadcast no waiter %d\n", pthread_cond_broadcast(&c));

    printf("static signal %d\n", pthread_cond_signal(&stc));
    printf("static broadcast %d\n", pthread_cond_broadcast(&stc));

    pthread_mutex_init(&m, NULL);
    pthread_mutex_lock(&m);
    clock_gettime(CLOCK_REALTIME, &past);
    past.tv_sec -= 2;
    r = pthread_cond_timedwait(&c, &m, &past);
    printf("past deadline ETIMEDOUT %d\n", r == ETIMEDOUT);
    r = pthread_mutex_unlock(&m);
    printf("mutex handed back %d\n", r);
    r = pthread_mutex_lock(&m);
    printf("relocks %d\n", r);
    pthread_mutex_unlock(&m);
    pthread_mutex_destroy(&m);

    printf("cond destroy %d\n", pthread_cond_destroy(&c));
    printf("static destroy %d\n", pthread_cond_destroy(&stc));
    return 0;
}
