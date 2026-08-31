/* filesystem slice: open flags and fcntl -- O_EXCL collisions, append
   mode observed through F_GETFL, F_DUPFD's floor, advisory locks via
   lockf and flock, fallocate and posix_fallocate observed through
   size, and the statvfs invariants. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

int main(void)
{
    char tmpl[] = "/tmp/esn-ofc-XXXXXX";
    char path[256];
    struct stat st;
    struct statvfs v, fv;
    int fd, fd2, r;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    snprintf(path, sizeof path, "%s/f", tmpl);

    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    printf("open-excl %d\n", fd >= 0);
    r = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    printf("excl-again %d %d\n", r, errno == EEXIST);

    r = fcntl(fd, F_GETFL);
    printf("getfl-wronly %d\n", (r & O_ACCMODE) == O_WRONLY);
    r = fcntl(fd, F_SETFL, r | O_APPEND);
    printf("setfl %d\n", r);
    r = fcntl(fd, F_GETFL);
    printf("appended %d\n", (r & O_APPEND) != 0);

    fd2 = fcntl(fd, F_DUPFD, 10);
    printf("dupfd-floor %d\n", fd2 >= 10);
    r = fcntl(fd2, F_GETFD);
    printf("getfd %d\n", r);
    close(fd2);

    r = lockf(fd, F_LOCK, 0);
    printf("lockf %d\n", r);
    r = lockf(fd, F_ULOCK, 0);
    printf("unlockf %d\n", r);
    r = flock(fd, LOCK_EX);
    printf("flock %d\n", r);
    r = flock(fd, LOCK_UN);
    printf("unflock %d\n", r);

    r = posix_fallocate(fd, 0, 4096);
    printf("posix-fallocate %d\n", r);
    fstat(fd, &st);
    printf("size-4k %d\n", (int)st.st_size == 4096);
    r = posix_fadvise(fd, 0, 4096, POSIX_FADV_SEQUENTIAL);
    printf("fadvise %d\n", r);
    close(fd);

    r = statvfs(tmpl, &v);
    printf("statvfs %d bsize %d blocks %d\n",
           r, v.f_bsize > 0, v.f_blocks > 0);
    fd = open(tmpl, O_RDONLY | O_DIRECTORY);
    r = fstatvfs(fd, &fv);
    printf("fstatvfs %d same-bsize %d\n",
           r, (long)fv.f_bsize == (long)v.f_bsize);
    close(fd);

    printf("umask-open ");
    umask(077);
    snprintf(path, sizeof path, "%s/g", tmpl);
    fd = open(path, O_CREAT | O_WRONLY, 0666);
    fstat(fd, &st);
    printf("%o\n", st.st_mode & 07777);
    close(fd);
    return 0;
}
