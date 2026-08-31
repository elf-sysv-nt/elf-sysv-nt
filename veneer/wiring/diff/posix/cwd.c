/* posix slice: the working directory -- chdir, fchdir, getcwd,
   get_current_dir_name. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    char buf[512], *dyn;
    int fd, r;

    fd = open(".", O_RDONLY);
    printf("save %d\n", fd >= 0);

    r = chdir("/tmp");
    printf("chdir %d\n", r);
    printf("getcwd %s\n", getcwd(buf, sizeof buf) ? buf : "(null)");
    dyn = get_current_dir_name();
    printf("dyn %d\n", dyn != NULL && strcmp(dyn, buf) == 0);
    free(dyn);

    r = chdir("/");
    printf("root %d\n", r);
    printf("getcwd2 %s\n", getcwd(buf, sizeof buf) ? buf : "(null)");

    r = fchdir(fd);
    printf("fchdir %d\n", r);
    r = close(fd);
    printf("close %d\n", r);

    r = chdir("/tmp/definitely-not-a-real-dir-esn");
    printf("bad %d\n", r == -1);
    return 0;
}
