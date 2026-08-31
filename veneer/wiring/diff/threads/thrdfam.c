/* threads slice: the C11 quartet that lives in libc — thrd_current,
   thrd_equal, thrd_sleep, thrd_yield. The current thread agrees with
   itself and a copy through thrd_equal; a short full sleep answers 0
   with the remainder untouched and time observably advanced past the
   request; and thrd_yield comes back at all, which is everything C11
   promises of it. */
#include <threads.h>
#include <stdio.h>
#include <time.h>

int main(void)
{
    thrd_t me = thrd_current(), copy = me;
    struct timespec want = { 0, 20 * 1000 * 1000 };
    struct timespec rem = { 7, 7 };
    struct timespec t0, t1;
    long long elapsed;
    int r;

    printf("current equals current %d\n",
           thrd_equal(thrd_current(), me) != 0);
    printf("copy equals current %d\n", thrd_equal(copy, me) != 0);

    timespec_get(&t0, TIME_UTC);
    r = thrd_sleep(&want, &rem);
    timespec_get(&t1, TIME_UTC);
    elapsed = (t1.tv_sec - t0.tv_sec) * 1000000000LL
        + (t1.tv_nsec - t0.tv_nsec);
    printf("sleep %d\n", r);
    printf("remainder untouched %d\n", rem.tv_sec == 7 && rem.tv_nsec == 7);
    printf("slept at least the request %d\n", elapsed >= 20 * 1000 * 1000);

    thrd_yield();
    printf("yield returned\n");
    return 0;
}
