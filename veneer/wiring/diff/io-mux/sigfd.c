/* io-mux slice: signalfd carrying a blocked signal as a readable fact --
   the descriptor reports the queued SIGUSR1's number and pid, and with
   nothing queued a nonblocking read is EAGAIN, not a hang. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/signalfd.h>

int main(void)
{
    sigset_t mask;
    struct signalfd_siginfo si;
    int fd;
    ssize_t n;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, 0) != 0)
        { printf("mask fail\n"); return 1; }

    fd = signalfd(-1, &mask, SFD_NONBLOCK);
    printf("open %d\n", fd >= 0);

    errno = 0;
    n = read(fd, &si, sizeof si);
    printf("idle %d %d\n", n == -1, errno == EAGAIN);

    kill(getpid(), SIGUSR1);
    n = read(fd, &si, sizeof si);
    printf("caught %d %d %d\n", n == (ssize_t)sizeof si,
           n > 0 && si.ssi_signo == SIGUSR1,
           n > 0 && (pid_t)si.ssi_pid == getpid());

    close(fd);
    return 0;
}
