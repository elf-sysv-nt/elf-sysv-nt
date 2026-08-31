/* signal slice: sigprocmask blocking, sigpending observing the held
   signal, unblocking delivering it, and the bad-how refusal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t hits;
static void on_usr1(int sig) { (void)sig; hits++; }

int main(void)
{
    sigset_t set, old, pend;
    struct sigaction sa;
    int rc;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_usr1;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    rc = sigprocmask(SIG_BLOCK, &set, &old);
    printf("block %d %d\n", rc == 0, sigismember(&old, SIGUSR1) == 0);

    raise(SIGUSR1);
    printf("held %d\n", hits == 0);

    rc = sigpending(&pend);
    printf("pending %d %d %d\n", rc == 0,
           sigismember(&pend, SIGUSR1) == 1,
           sigismember(&pend, SIGUSR2) == 0);

    rc = sigprocmask(SIG_UNBLOCK, &set, NULL);
    printf("unblock %d %d\n", rc == 0, hits == 1);

    rc = sigpending(&pend);
    printf("drained %d %d\n", rc == 0, sigisemptyset(&pend) != 0);

    rc = sigprocmask(SIG_SETMASK, &old, NULL);
    printf("restore %d\n", rc == 0);

    errno = 0;
    rc = sigprocmask(12345, &set, NULL);
    printf("bad-how %d %d\n", rc == -1, errno == EINVAL);

    sigemptyset(&set);
    rc = sigprocmask(SIG_BLOCK, NULL, &set);
    printf("query-only %d %d\n", rc == 0, sigismember(&set, SIGUSR1) == 0);
    return 0;
}
