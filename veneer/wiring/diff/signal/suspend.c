/* signal slice: sigsuspend over an already-pending signal -- the
   handler runs, the call refuses EINTR, and the caller's mask comes
   back untouched -- plus siginterrupt's refusals. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t hits;
static void on_usr1(int sig) { (void)sig; hits++; }

int main(void)
{
    sigset_t block, empty, after;
    struct sigaction sa;
    int rc;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_usr1;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, NULL);
    raise(SIGUSR1);
    printf("pending %d\n", hits == 0);

    sigemptyset(&empty);
    errno = 0;
    rc = sigsuspend(&empty);
    printf("suspend %d %d %d\n", rc == -1, errno == EINTR, hits == 1);

    sigprocmask(SIG_BLOCK, NULL, &after);
    printf("mask-back %d\n", sigismember(&after, SIGUSR1) == 1);
    sigprocmask(SIG_UNBLOCK, &block, NULL);

    rc = siginterrupt(SIGUSR1, 1);
    printf("intr-on %d\n", rc == 0);
    rc = siginterrupt(SIGUSR1, 0);
    printf("intr-off %d\n", rc == 0);
    errno = 0;
    rc = siginterrupt(0, 1);
    printf("intr-bad %d %d\n", rc == -1, errno == EINVAL);
    return 0;
}
