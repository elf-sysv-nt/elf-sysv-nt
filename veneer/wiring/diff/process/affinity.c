/* process slice: the affinity pair -- the caller's own mask read back
   non-empty, written back unchanged, and the empty-mask refusal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sched.h>

int main(void)
{
    cpu_set_t mask, empty;
    int rc;

    CPU_ZERO(&mask);
    rc = sched_getaffinity(0, sizeof mask, &mask);
    printf("get %d %d\n", rc == 0, CPU_COUNT(&mask) > 0);

    rc = sched_setaffinity(0, sizeof mask, &mask);
    printf("set-same %d\n", rc == 0);

    CPU_ZERO(&empty);
    errno = 0;
    rc = sched_setaffinity(0, sizeof empty, &empty);
    printf("set-empty %d %d\n", rc == -1, errno == EINVAL);

    CPU_ZERO(&mask);
    rc = sched_getaffinity(0, sizeof mask, &mask);
    printf("still %d %d\n", rc == 0, CPU_COUNT(&mask) > 0);
    return 0;
}
