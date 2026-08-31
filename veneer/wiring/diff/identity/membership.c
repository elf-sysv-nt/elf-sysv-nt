/* identity slice: group membership -- getgrouplist's two-call
   protocol (the short first call refuses and reports the count, the
   sized second call delivers root's own gid), initgroups refusing a
   user that is not there, and setgroups refusing an impossible
   count with EINVAL before it looks at privilege. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

int main(void)
{
    gid_t groups[64];
    int n, rc, i, has0;

    n = 0;
    rc = getgrouplist("root", 0, groups, &n);
    printf("short %d %d\n", rc == -1, n >= 1);

    n = 64;
    rc = getgrouplist("root", 0, groups, &n);
    has0 = 0;
    for (i = 0; i < n; i++)
        if (groups[i] == 0)
            has0 = 1;
    printf("sized %d %d %d\n", rc == n, n >= 1, has0);

    rc = initgroups("esn-no-such-user", 0);
    printf("init-no-such %d\n", rc == -1);

    errno = 0;
    rc = setgroups((size_t)1 << 30, 0);
    printf("set-huge %d %d\n", rc == -1, errno == EINVAL);
    return 0;
}
