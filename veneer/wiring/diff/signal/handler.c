/* signal slice: sigaction installing a handler, raise delivering it
   synchronously, the third-argument sa_mask observed inside the
   handler, and the one-shot semantics of sysv_signal against the
   BSD semantics of signal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t hits;
static volatile sig_atomic_t masked_inside;

static void on_usr1(int sig)
{
    sigset_t cur;
    hits++;
    sigprocmask(SIG_BLOCK, NULL, &cur);
    masked_inside = sigismember(&cur, SIGUSR2);
    (void)sig;
}

static void on_usr2(int sig) { (void)sig; hits += 10; }

int main(void)
{
    struct sigaction sa, old;
    int rc;

    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR2);
    sa.sa_handler = on_usr1;
    sa.sa_flags = 0;
    rc = sigaction(SIGUSR1, &sa, NULL);
    printf("install %d\n", rc == 0);

    rc = raise(SIGUSR1);
    printf("raise %d %d %d\n", rc == 0, hits == 1, masked_inside == 1);

    rc = sigaction(SIGUSR1, NULL, &old);
    printf("query %d %d %d\n", rc == 0, old.sa_handler == on_usr1,
           sigismember(&old.sa_mask, SIGUSR2) == 1);

    errno = 0;
    rc = sigaction(SIGKILL, &sa, NULL);
    printf("kill-refused %d %d\n", rc == -1, errno == EINVAL);
    errno = 0;
    rc = sigaction(0, &sa, NULL);
    printf("zero-refused %d %d\n", rc == -1, errno == EINVAL);

    hits = 0;
    printf("signal-install %d\n", signal(SIGUSR2, on_usr2) != SIG_ERR);
    raise(SIGUSR2);
    raise(SIGUSR2);
    printf("signal-sticky %d %d\n", hits == 20,
           signal(SIGUSR2, SIG_DFL) == on_usr2);

    hits = 0;
    printf("sysv-install %d\n", sysv_signal(SIGUSR2, on_usr2) != SIG_ERR);
    raise(SIGUSR2);
    printf("sysv-oneshot %d %d\n", hits == 10,
           sysv_signal(SIGUSR2, SIG_DFL) == SIG_DFL);
    return 0;
}
