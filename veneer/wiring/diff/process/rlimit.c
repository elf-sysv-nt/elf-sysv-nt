/* process slice: the rlimit shims-to-be -- reading NOFILE, lowering
   the soft limit and observing it stick, cur-above-max and bad-resource
   refused EINVAL, and the 64 spelling agreeing with the plain one. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <sys/resource.h>

int main(void)
{
    struct rlimit rl, rl2;
    struct rlimit64 rl64;
    int rc;

    rc = getrlimit(RLIMIT_NOFILE, &rl);
    printf("get %d %d\n", rc == 0, rl.rlim_cur <= rl.rlim_max);

    rc = getrlimit64(RLIMIT_NOFILE, &rl64);
    printf("get64 %d %d %d\n", rc == 0,
           (rlim64_t)rl.rlim_cur == rl64.rlim_cur,
           (rlim64_t)rl.rlim_max == rl64.rlim_max);

    rl2 = rl;
    rl2.rlim_cur = rl.rlim_cur > 64 ? rl.rlim_cur - 1 : rl.rlim_cur;
    rc = setrlimit(RLIMIT_NOFILE, &rl2);
    printf("lower %d\n", rc == 0);
    rc = getrlimit(RLIMIT_NOFILE, &rl);
    printf("stuck %d %d\n", rc == 0, rl.rlim_cur == rl2.rlim_cur);

    rl2.rlim_cur = rl2.rlim_max + 1;
    errno = 0;
    rc = setrlimit(RLIMIT_NOFILE, &rl2);
    printf("cur-over-max %d %d\n", rc == -1, errno == EINVAL);

    errno = 0;
    rc = getrlimit(1000, &rl);
    printf("bad-resource %d %d\n", rc == -1, errno == EINVAL);
    return 0;
}
