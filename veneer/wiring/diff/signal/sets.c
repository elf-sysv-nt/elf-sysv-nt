/* signal slice: the sigset_t manipulation family -- sigemptyset,
   sigfillset, sigaddset, sigdelset, sigismember and the GNU set
   algebra -- with the out-of-range refusals. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>

int main(void)
{
    sigset_t a, b, c;
    int rc;

    rc = sigemptyset(&a);
    printf("empty %d %d\n", rc == 0, sigisemptyset(&a) != 0);

    rc = sigfillset(&b);
    printf("fill %d %d\n", rc == 0, sigisemptyset(&b) == 0);
    printf("fill-has %d %d %d\n", sigismember(&b, SIGINT) == 1,
           sigismember(&b, SIGTERM) == 1, sigismember(&b, SIGUSR2) == 1);

    rc = sigaddset(&a, SIGUSR1);
    printf("add %d %d %d\n", rc == 0, sigismember(&a, SIGUSR1) == 1,
           sigismember(&a, SIGUSR2) == 0);

    rc = sigdelset(&a, SIGUSR1);
    printf("del %d %d\n", rc == 0, sigisemptyset(&a) != 0);

    errno = 0;
    rc = sigaddset(&a, 0);
    printf("add-zero %d %d\n", rc == -1, errno == EINVAL);
    errno = 0;
    rc = sigaddset(&a, 100000);
    printf("add-huge %d %d\n", rc == -1, errno == EINVAL);
    errno = 0;
    rc = sigismember(&a, 0);
    printf("member-zero %d %d\n", rc == -1, errno == EINVAL);

    sigemptyset(&a);
    sigaddset(&a, SIGUSR1);
    sigaddset(&a, SIGUSR2);
    sigemptyset(&b);
    sigaddset(&b, SIGUSR2);
    sigaddset(&b, SIGTERM);

    rc = sigandset(&c, &a, &b);
    printf("and %d %d %d %d\n", rc == 0,
           sigismember(&c, SIGUSR2) == 1,
           sigismember(&c, SIGUSR1) == 0,
           sigismember(&c, SIGTERM) == 0);

    rc = sigorset(&c, &a, &b);
    printf("or %d %d %d %d\n", rc == 0,
           sigismember(&c, SIGUSR1) == 1,
           sigismember(&c, SIGUSR2) == 1,
           sigismember(&c, SIGTERM) == 1);
    return 0;
}
