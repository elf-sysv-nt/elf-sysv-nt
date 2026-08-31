/* signal slice: the naming surface -- strsignal over the classic
   numbers and an unknown one, psignal and psiginfo with stderr
   folded onto stdout so their lines are observable. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

int main(void)
{
    siginfo_t info;

    printf("hup %s\n", strsignal(SIGHUP));
    printf("int %s\n", strsignal(SIGINT));
    printf("segv %s\n", strsignal(SIGSEGV));
    printf("term %s\n", strsignal(SIGTERM));
    printf("usr1 %s\n", strsignal(SIGUSR1));

    fflush(stdout);
    dup2(STDOUT_FILENO, STDERR_FILENO);

    psignal(SIGINT, "psig");
    psignal(SIGTERM, NULL);
    fflush(stderr);

    memset(&info, 0, sizeof info);
    info.si_signo = SIGUSR1;
    info.si_code = SI_USER;
    psiginfo(&info, "pinfo");
    fflush(stderr);
    return 0;
}
