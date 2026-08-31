/* time slice: setting file times -- utimes, futimes, lutimes,
   futimesat -- to fixed instants, read back through stat so every
   printed value is the one we set. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

int main(void)
{
    char dir[] = "/tmp/esn-ft-XXXXXX";
    char path[128], link[128];
    struct timeval tv[2];
    struct stat st;
    int fd;

    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(path, sizeof path, "%s/f", dir);
    snprintf(link, sizeof link, "%s/l", dir);
    fd = open(path, O_CREAT | O_WRONLY, 0644);
    printf("open %d\n", fd >= 0);

    tv[0].tv_sec = 1000000000; tv[0].tv_usec = 0;   /* atime */
    tv[1].tv_sec = 1000000500; tv[1].tv_usec = 0;   /* mtime */
    printf("utimes %d\n", utimes(path, tv) == 0);
    printf("utimes-read %d %d\n",
           stat(path, &st) == 0 ? (int)st.st_atime : -1,
           (int)st.st_mtime);

    tv[0].tv_sec = 1100000000; tv[1].tv_sec = 1100000500;
    printf("futimes %d\n", futimes(fd, tv) == 0);
    printf("futimes-read %d %d\n",
           fstat(fd, &st) == 0 ? (int)st.st_atime : -1,
           (int)st.st_mtime);

    tv[0].tv_sec = 1200000000; tv[1].tv_sec = 1200000500;
    printf("futimesat %d\n", futimesat(AT_FDCWD, path, tv) == 0);
    printf("futimesat-read %d %d\n",
           stat(path, &st) == 0 ? (int)st.st_atime : -1,
           (int)st.st_mtime);

    /* lutimes touches the link itself, not the file behind it. */
    printf("symlink %d\n", symlink(path, link) == 0);
    tv[0].tv_sec = 1300000000; tv[1].tv_sec = 1300000500;
    printf("lutimes %d\n", lutimes(link, tv) == 0);
    printf("lutimes-link %d %d\n",
           lstat(link, &st) == 0 ? (int)st.st_atime : -1,
           (int)st.st_mtime);
    printf("lutimes-target %d %d\n",
           stat(path, &st) == 0 ? (int)st.st_atime : -1,
           (int)st.st_mtime);

    close(fd);
    unlink(link);
    unlink(path);
    rmdir(dir);
    return 0;
}
