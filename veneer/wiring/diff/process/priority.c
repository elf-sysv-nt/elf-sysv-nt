/* process slice: the nice surface -- getpriority's errno protocol on
   the caller, setpriority making the process nicer and the change
   observed, and the bad-which refusal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/resource.h>

int main(void)
{
    int rc;

    errno = 0;
    rc = getpriority(PRIO_PROCESS, 0);
    printf("get %d %d\n", rc == 0, errno == 0);

    rc = setpriority(PRIO_PROCESS, 0, 5);
    printf("set %d\n", rc == 0);
    errno = 0;
    rc = getpriority(PRIO_PROCESS, 0);
    printf("nicer %d %d\n", rc == 5, errno == 0);

    errno = 0;
    rc = getpriority(1000, 0);
    printf("bad-which %d %d\n", rc == -1, errno == EINVAL);

    errno = 0;
    rc = setpriority(PRIO_PROCESS, 999999, 5);
    printf("no-such %d %d\n", rc == -1, errno == ESRCH);
    return 0;
}
