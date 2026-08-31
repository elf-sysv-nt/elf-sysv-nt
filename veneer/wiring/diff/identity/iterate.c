/* identity slice: iteration -- setpwent through getpwent to endpwent
   walks the whole database and passes root exactly once, setgrent
   rewinds the group walk the same way, and a second pass after the
   rewind finds root again. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

int main(void)
{
    struct passwd *pw;
    struct group *gr;
    int n, roots;

    setpwent();
    n = roots = 0;
    while ((pw = getpwent()) != 0) {
        n++;
        if (strcmp(pw->pw_name, "root") == 0 && pw->pw_uid == 0)
            roots++;
    }
    endpwent();
    printf("pw-walk %d %d\n", n > 0, roots == 1);

    setgrent();
    n = roots = 0;
    while ((gr = getgrent()) != 0) {
        n++;
        if (strcmp(gr->gr_name, "root") == 0 && gr->gr_gid == 0)
            roots++;
    }
    endgrent();
    printf("gr-walk %d %d\n", n > 0, roots == 1);

    setpwent();
    roots = 0;
    while ((pw = getpwent()) != 0)
        if (pw->pw_uid == 0)
            roots++;
    endpwent();
    printf("rewind %d\n", roots >= 1);
    return 0;
}
