/* signal slice: the System V holding surface -- sighold, sigrelse,
   sigset, sigignore -- and killpg on the caller's own group. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t hits;
static void on_usr1(int sig) { (void)sig; hits++; }

int main(void)
{
    sigset_t pend;
    int rc;

    signal(SIGUSR1, on_usr1);

    rc = sighold(SIGUSR1);
    printf("hold %d\n", rc == 0);
    raise(SIGUSR1);
    sigpending(&pend);
    printf("held %d %d\n", hits == 0, sigismember(&pend, SIGUSR1) == 1);

    rc = sigrelse(SIGUSR1);
    printf("relse %d %d\n", rc == 0, hits == 1);

    hits = 0;
    printf("sigset %d\n", sigset(SIGUSR1, on_usr1) == on_usr1);
    raise(SIGUSR1);
    printf("sigset-fires %d\n", hits == 1);
    printf("sigset-hold %d\n", sigset(SIGUSR1, SIG_HOLD) == on_usr1);
    raise(SIGUSR1);
    sigpending(&pend);
    printf("sigset-held %d %d\n", hits == 1,
           sigismember(&pend, SIGUSR1) == 1);
    printf("sigset-back %d %d\n", sigset(SIGUSR1, on_usr1) == SIG_HOLD,
           hits == 2);

    rc = sigignore(SIGUSR2);
    printf("ignore %d\n", rc == 0);
    raise(SIGUSR2);
    printf("ignored %d\n", hits == 2);

    errno = 0;
    rc = sighold(0);
    printf("hold-bad %d %d\n", rc == -1, errno == EINVAL);

    hits = 0;
    signal(SIGUSR1, on_usr1);
    rc = killpg(getpgrp(), SIGUSR1);
    printf("killpg %d %d\n", rc == 0, hits == 1);
    errno = 0;
    rc = killpg(1, 0);
    printf("killpg-probe %d\n", rc == -1 ? errno == EPERM : rc == 0);
    return 0;
}
