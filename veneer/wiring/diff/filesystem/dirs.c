/* filesystem slice: making nodes -- umask, mkdir, mkdirat, chmod,
   fchmod, fchmodat, mkfifo, mkfifoat, observed through the stat
   family (the __xstat shims-to-be). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(void)
{
    char tmpl[] = "/tmp/esn-dirs-XXXXXX";
    char path[256];
    struct stat st;
    mode_t old;
    int r, dfd;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }

    old = umask(027);
    printf("umask-prev-is-mode %d\n", umask(027) == 027);
    snprintf(path, sizeof path, "%s/d1", tmpl);
    r = mkdir(path, 0777);
    printf("mkdir %d\n", r);
    r = stat(path, &st);
    printf("d1 stat %d dir %d mode %o\n",
           r, S_ISDIR(st.st_mode) != 0, st.st_mode & 07777);

    r = chmod(path, 0700);
    printf("chmod %d\n", r);
    stat(path, &st);
    printf("d1 mode %o\n", st.st_mode & 07777);

    dfd = open(tmpl, O_RDONLY | O_DIRECTORY);
    printf("dirfd-open %d\n", dfd >= 0);
    r = mkdirat(dfd, "d2", 0750);
    printf("mkdirat %d\n", r);
    r = fstatat(dfd, "d2", &st, 0);
    printf("d2 fstatat %d dir %d mode %o\n",
           r, S_ISDIR(st.st_mode) != 0, st.st_mode & 07777);
    r = fchmodat(dfd, "d2", 0755, 0);
    printf("fchmodat %d\n", r);
    fstatat(dfd, "d2", &st, 0);
    printf("d2 mode %o\n", st.st_mode & 07777);

    r = mkfifo(path, 0644);
    printf("mkfifo-exists %d %d\n", r, errno == EEXIST);
    snprintf(path, sizeof path, "%s/p1", tmpl);
    r = mkfifo(path, 0666);
    printf("mkfifo %d\n", r);
    lstat(path, &st);
    printf("p1 fifo %d mode %o\n", S_ISFIFO(st.st_mode) != 0,
           st.st_mode & 07777);
    r = mkfifoat(dfd, "p2", 0600);
    printf("mkfifoat %d\n", r);
    fstatat(dfd, "p2", &st, AT_SYMLINK_NOFOLLOW);
    printf("p2 fifo %d mode %o\n", S_ISFIFO(st.st_mode) != 0,
           st.st_mode & 07777);

    snprintf(path, sizeof path, "%s/f1", tmpl);
    r = creat(path, 0666);
    printf("creat %d\n", r >= 0);
    if (r >= 0) {
        int fd = r;
        r = fchmod(fd, 0640);
        printf("fchmod %d\n", r);
        fstat(fd, &st);
        printf("f1 reg %d mode %o size %d\n", S_ISREG(st.st_mode) != 0,
               st.st_mode & 07777, (int)st.st_size);
        close(fd);
    }

    umask(old);
    r = stat("/tmp/esn-no-such-thing", &st);
    printf("stat-missing %d %d\n", r, errno == ENOENT);
    return 0;
}
