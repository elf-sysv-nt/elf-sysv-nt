/* filesystem slice: file times -- utime, utimensat, futimens, with
   UTIME_OMIT holding one side still, read back through stat. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>

int main(void)
{
    char tmpl[] = "/tmp/esn-times-XXXXXX";
    char path[256];
    struct utimbuf ub;
    struct timespec ts[2];
    struct stat st;
    int fd, r;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    snprintf(path, sizeof path, "%s/f", tmpl);
    fd = open(path, O_CREAT | O_WRONLY, 0644);
    printf("open %d\n", fd >= 0);

    ub.actime = 1000000000;
    ub.modtime = 1000000100;
    r = utime(path, &ub);
    printf("utime %d\n", r);
    stat(path, &st);
    printf("utime-times %ld %ld\n", (long)st.st_atime, (long)st.st_mtime);

    ts[0].tv_sec = 1200000000; ts[0].tv_nsec = 500000000;
    ts[1].tv_sec = 1200000100; ts[1].tv_nsec = 250000000;
    r = utimensat(AT_FDCWD, path, ts, 0);
    printf("utimensat %d\n", r);
    stat(path, &st);
    printf("nsat-times %ld.%09ld %ld.%09ld\n",
           (long)st.st_atim.tv_sec, st.st_atim.tv_nsec,
           (long)st.st_mtim.tv_sec, st.st_mtim.tv_nsec);

    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = 1300000000; ts[1].tv_nsec = 0;
    r = futimens(fd, ts);
    printf("futimens %d\n", r);
    fstat(fd, &st);
    printf("omit-held %d mtime %ld\n",
           (long)st.st_atim.tv_sec == 1200000000, (long)st.st_mtime);

    close(fd);
    return 0;
}
