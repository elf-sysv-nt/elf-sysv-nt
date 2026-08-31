/* process slice: the spawn attribute object -- every getter observing
   what its setter stored, over flags, the process group, the sched
   policy and parameter, and both signal sets. */
#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>

int main(void)
{
    posix_spawnattr_t at;
    struct sched_param sp, sp2;
    sigset_t set, out;
    short flags = 0;
    pid_t pg = 0;
    int rc, pol = -1;

    rc = posix_spawnattr_init(&at);
    printf("init %d\n", rc == 0);

    rc = posix_spawnattr_setflags(&at,
        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    posix_spawnattr_getflags(&at, &flags);
    printf("flags %d %d\n", rc == 0,
           flags == (POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK));

    rc = posix_spawnattr_setpgroup(&at, 4242);
    posix_spawnattr_getpgroup(&at, &pg);
    printf("pgroup %d %d\n", rc == 0, pg == 4242);

    rc = posix_spawnattr_setschedpolicy(&at, SCHED_FIFO);
    posix_spawnattr_getschedpolicy(&at, &pol);
    printf("policy %d %d\n", rc == 0, pol == SCHED_FIFO);

    sp.sched_priority = 17;
    rc = posix_spawnattr_setschedparam(&at, &sp);
    posix_spawnattr_getschedparam(&at, &sp2);
    printf("param %d %d\n", rc == 0, sp2.sched_priority == 17);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    rc = posix_spawnattr_setsigmask(&at, &set);
    sigemptyset(&out);
    posix_spawnattr_getsigmask(&at, &out);
    printf("sigmask %d %d %d\n", rc == 0,
           sigismember(&out, SIGUSR1), sigismember(&out, SIGUSR2));

    sigaddset(&set, SIGTERM);
    rc = posix_spawnattr_setsigdefault(&at, &set);
    sigemptyset(&out);
    posix_spawnattr_getsigdefault(&at, &out);
    printf("sigdefault %d %d %d\n", rc == 0,
           sigismember(&out, SIGTERM), sigismember(&out, SIGHUP));

    rc = posix_spawnattr_destroy(&at);
    printf("destroy %d\n", rc == 0);
    return 0;
}
