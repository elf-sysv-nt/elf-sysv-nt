/* process slice: the wait family over real children -- waitpid seeing
   an exit status and a termination signal through the status macros,
   wait draining the last child, and ECHILD once nothing is left. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t pid, rc;
    int status;

    pid = fork();
    if (pid == 0)
        _exit(23);
    rc = waitpid(pid, &status, 0);
    printf("exit %d %d %d\n", rc == pid, WIFEXITED(status),
           WEXITSTATUS(status) == 23);

    pid = fork();
    if (pid == 0) {
        raise(SIGKILL);
        _exit(99);
    }
    rc = waitpid(pid, &status, 0);
    printf("signal %d %d %d %d\n", rc == pid, WIFSIGNALED(status),
           WTERMSIG(status) == SIGKILL, WIFEXITED(status));

    pid = fork();
    if (pid == 0)
        _exit(0);
    rc = wait(&status);
    printf("wait %d %d %d\n", rc == pid, WIFEXITED(status),
           WEXITSTATUS(status) == 0);

    errno = 0;
    rc = wait(&status);
    printf("drained %d %d\n", rc == -1, errno == ECHILD);

    errno = 0;
    rc = waitpid(-1, &status, WNOHANG);
    printf("nohang-empty %d %d\n", rc == -1, errno == ECHILD);
    return 0;
}
