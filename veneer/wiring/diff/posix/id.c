/* posix slice: identity and limits -- printed as invariants, never raw
   values, so the two sides need not share uids or page sizes. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char host[256], swapped[8];
    long v;
    int r;

    printf("pid %d\n", getpid() > 0);
    printf("ppid %d\n", getppid() > 0 && getppid() != getpid());
    printf("uid %d\n", getuid() == geteuid());
    printf("gid %d\n", getgid() == getegid());
    printf("pgrp %d\n", getpgrp() == getpgid(0));

    v = sysconf(_SC_PAGESIZE);
    printf("page %d %d\n", v > 0, (int)v == getpagesize());
    v = sysconf(_SC_OPEN_MAX);
    printf("nofile %d %d\n", v > 0, (int)v == getdtablesize());
    printf("clk %d\n", sysconf(_SC_CLK_TCK) > 0);

    r = gethostname(host, sizeof host);
    printf("host %d %d\n", r, host[0] != 0);

    swab("badc", swapped, 4);
    swapped[4] = 0;
    printf("swab %s\n", swapped);

    printf("alarm %d\n", alarm(0) == 0);
    printf("sleep %d\n", sleep(0) == 0);
    printf("usleep %d\n", usleep(1000) == 0);
    return 0;
}
