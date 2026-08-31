/* terminal slice: the utmp and utmpx session records over private
   files -- utmpname and utmpxname point the walkers at temp files, so
   no system database and no privilege decides anything. Records are
   written with pututline/pututxline, found again by id and by line
   after a rewind, missed when nothing matches, and updwtmp grows a
   wtmp file by exactly one record. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <utmp.h>
#include <utmpx.h>

int main(void)
{
    char upath[] = "/tmp/esn-utmp-XXXXXX";
    char xpath[] = "/tmp/esn-utmpx-XXXXXX";
    char wpath[] = "/tmp/esn-wtmp-XXXXXX";
    struct utmp u, key, *found;
    struct utmpx ux, xkey, *xfound;
    struct stat st;
    int fd, r;
    off_t before;

    fd = mkstemp(upath); if (fd < 0) { printf("mkstemp fail\n"); return 1; }
    close(fd);
    fd = mkstemp(xpath); if (fd < 0) { printf("mkstemp fail\n"); return 1; }
    close(fd);
    fd = mkstemp(wpath); if (fd < 0) { printf("mkstemp fail\n"); return 1; }
    close(fd);

    r = utmpname(upath);
    printf("name %d\n", r);

    memset(&u, 0, sizeof u);
    u.ut_type = USER_PROCESS;
    u.ut_pid = 1234;
    strcpy(u.ut_line, "pts/7");
    strcpy(u.ut_id, "es1");
    strcpy(u.ut_user, "esnuser");
    setutent();
    found = pututline(&u);
    printf("put %d\n", found != NULL);

    memset(&u, 0, sizeof u);
    u.ut_type = USER_PROCESS;
    u.ut_pid = 5678;
    strcpy(u.ut_line, "pts/8");
    strcpy(u.ut_id, "es2");
    strcpy(u.ut_user, "esnother");
    found = pututline(&u);
    printf("put2 %d\n", found != NULL);

    setutent();
    memset(&key, 0, sizeof key);
    key.ut_type = USER_PROCESS;
    strcpy(key.ut_id, "es2");
    found = getutid(&key);
    printf("byid %d %d %d\n", found != NULL,
           found && found->ut_pid == 5678,
           found && strcmp(found->ut_user, "esnother") == 0);

    setutent();
    memset(&key, 0, sizeof key);
    strcpy(key.ut_line, "pts/7");
    found = getutline(&key);
    printf("byline %d %d %d\n", found != NULL,
           found && found->ut_pid == 1234,
           found && strcmp(found->ut_user, "esnuser") == 0);

    setutent();
    memset(&key, 0, sizeof key);
    strcpy(key.ut_line, "pts/9");
    found = getutline(&key);
    printf("miss %d\n", found == NULL);
    endutent();

    r = utmpxname(xpath);
    printf("xname %d\n", r);
    memset(&ux, 0, sizeof ux);
    ux.ut_type = USER_PROCESS;
    ux.ut_pid = 4321;
    strcpy(ux.ut_line, "pts/5");
    strcpy(ux.ut_id, "ex1");
    strcpy(ux.ut_user, "esnxuser");
    setutxent();
    xfound = pututxline(&ux);
    printf("xput %d\n", xfound != NULL);
    setutxent();
    memset(&xkey, 0, sizeof xkey);
    strcpy(xkey.ut_line, "pts/5");
    xfound = getutxline(&xkey);
    printf("xbyline %d %d %d\n", xfound != NULL,
           xfound && xfound->ut_pid == 4321,
           xfound && strcmp(xfound->ut_user, "esnxuser") == 0);
    endutxent();

    if (stat(wpath, &st) != 0) { printf("stat fail\n"); return 1; }
    before = st.st_size;
    memset(&u, 0, sizeof u);
    u.ut_type = USER_PROCESS;
    u.ut_pid = 9999;
    strcpy(u.ut_line, "pts/6");
    strcpy(u.ut_user, "esnwtmp");
    updwtmp(wpath, &u);
    if (stat(wpath, &st) != 0) { printf("stat fail\n"); return 1; }
    printf("wtmp %d\n", st.st_size - before == (off_t)sizeof(struct utmp));

    unlink(upath); unlink(xpath); unlink(wpath);
    return 0;
}
