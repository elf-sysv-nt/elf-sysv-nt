/* identity slice: the _r variants -- each agreeing with its plain
   sibling on root, the ERANGE refusal when the caller's buffer cannot
   hold the strings, and not-found as success with a NULL result. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

int main(void)
{
    char buf[4096], tiny[4];
    struct passwd pw, *pwp;
    struct group gr, *grp;
    int rc;

    rc = getpwnam_r("root", &pw, buf, sizeof buf, &pwp);
    printf("pwnam_r %d %d %d %d\n", rc == 0, pwp == &pw,
           pwp && pwp->pw_uid == 0,
           pwp && strcmp(pwp->pw_name, "root") == 0);

    rc = getpwuid_r(0, &pw, buf, sizeof buf, &pwp);
    printf("pwuid_r %d %d %d\n", rc == 0, pwp != 0,
           pwp && strcmp(pwp->pw_name, "root") == 0);

    rc = getgrnam_r("root", &gr, buf, sizeof buf, &grp);
    printf("grnam_r %d %d %d\n", rc == 0, grp != 0,
           grp && grp->gr_gid == 0);

    rc = getgrgid_r(0, &gr, buf, sizeof buf, &grp);
    printf("grgid_r %d %d %d\n", rc == 0, grp != 0,
           grp && strcmp(grp->gr_name, "root") == 0);

    rc = getpwnam_r("root", &pw, tiny, sizeof tiny, &pwp);
    printf("pw-tiny %d %d\n", rc == ERANGE, pwp == 0);

    rc = getgrgid_r(0, &gr, tiny, sizeof tiny, &grp);
    printf("gr-tiny %d %d\n", rc == ERANGE, grp == 0);

    rc = getpwnam_r("esn-no-such-user", &pw, buf, sizeof buf, &pwp);
    printf("no-such %d %d\n", rc == 0, pwp == 0);
    return 0;
}
