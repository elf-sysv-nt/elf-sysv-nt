/* signal slice: the synchronous wait family -- sigwait, sigwaitinfo,
   sigtimedwait with a zero timeout refusing EAGAIN -- and sigqueue
   carrying its payload through siginfo_t. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

int main(void)
{
    sigset_t set;
    siginfo_t info;
    struct timespec zero = { 0, 0 };
    union sigval val;
    int sig = 0, rc;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);

    raise(SIGUSR1);
    rc = sigwait(&set, &sig);
    printf("sigwait %d %d\n", rc == 0, sig == SIGUSR1);

    val.sival_int = 4242;
    rc = sigqueue(getpid(), SIGUSR1, val);
    printf("queue %d\n", rc == 0);
    rc = sigwaitinfo(&set, &info);
    printf("waitinfo %d %d %d %d\n", rc == SIGUSR1,
           info.si_signo == SIGUSR1, info.si_code == SI_QUEUE,
           info.si_value.sival_int == 4242);

    raise(SIGUSR1);
    rc = sigtimedwait(&set, &info, &zero);
    printf("timedwait %d %d\n", rc == SIGUSR1, info.si_code == SI_USER);

    errno = 0;
    rc = sigtimedwait(&set, &info, &zero);
    printf("timedwait-empty %d %d\n", rc == -1, errno == EAGAIN);

    errno = 0;
    rc = sigqueue(getpid(), -1, val);
    printf("queue-bad %d %d\n", rc == -1, errno == EINVAL);

    errno = 0;
    rc = kill(getpid(), 0);
    printf("kill-probe %d %d\n", rc == 0, errno == 0);
    errno = 0;
    rc = kill(getpid(), -1);
    printf("kill-bad %d %d\n", rc == -1, errno == EINVAL);
    return 0;
}
