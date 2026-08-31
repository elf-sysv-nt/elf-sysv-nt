/* process slice: rusage -- getrusage on the caller with the bad-who
   refusal, and wait3/wait4 filling a child's rusage alongside its
   status. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    struct rusage ru;
    pid_t pid, rc;
    int status;

    errno = 0;
    rc = getrusage(RUSAGE_SELF, &ru);
    printf("self %d %d %ld\n", rc == 0, errno == 0,
           (long)(ru.ru_maxrss > 0));

    errno = 0;
    rc = getrusage(1000, &ru);
    printf("bad-who %d %d\n", rc == -1, errno == EINVAL);

    pid = fork();
    if (pid == 0)
        _exit(7);
    rc = wait4(pid, &status, 0, &ru);
    printf("wait4 %d %d %d\n", rc == pid, WIFEXITED(status),
           WEXITSTATUS(status) == 7);

    pid = fork();
    if (pid == 0)
        _exit(3);
    rc = wait3(&status, 0, &ru);
    printf("wait3 %d %d %d\n", rc == pid, WIFEXITED(status),
           WEXITSTATUS(status) == 3);
    return 0;
}
