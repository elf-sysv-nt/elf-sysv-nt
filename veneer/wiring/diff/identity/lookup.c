/* identity slice: lookup by name and by id -- getpwnam and getpwuid
   agreeing on root, getgrnam and getgrgid agreeing on root's group,
   and the not-found answer as a NULL that sets no errno lie. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

int main(void)
{
    struct passwd *pw, *pu;
    struct group *gr, *gg;

    pw = getpwnam("root");
    printf("pwnam %d %d %d %d\n", pw != 0,
           pw && pw->pw_uid == 0, pw && pw->pw_gid == 0,
           pw && strcmp(pw->pw_name, "root") == 0);

    pu = getpwuid(0);
    printf("pwuid %d %d %d\n", pu != 0,
           pu && pu->pw_uid == 0,
           pu && strcmp(pu->pw_name, "root") == 0);

    gr = getgrnam("root");
    printf("grnam %d %d %d\n", gr != 0,
           gr && gr->gr_gid == 0,
           gr && strcmp(gr->gr_name, "root") == 0);

    gg = getgrgid(0);
    printf("grgid %d %d\n", gg != 0,
           gg && strcmp(gg->gr_name, "root") == 0);

    printf("dir-shell %d %d\n",
           pw && pw->pw_dir && pw->pw_dir[0] == '/',
           pw && pw->pw_shell && pw->pw_shell[0] == '/');

    printf("no-such %d %d\n",
           getpwnam("esn-no-such-user") == 0,
           getgrnam("esn-no-such-group") == 0);
    return 0;
}
