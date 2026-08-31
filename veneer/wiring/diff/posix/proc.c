/* posix slice: processes -- fork, _exit, execl, execvp, and the pipe
   between the halves.  wait comes from sys/wait, outside the slice,
   but the case needs the join. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    char buf[64];
    int p[2], st, r;
    pid_t c;
    ssize_t n;

    r = pipe(p);
    printf("pipe %d\n", r);
    c = fork();
    if (c == 0) {
        r = (int)write(p[1], "child", 5);
        _exit(r == 5 ? 7 : 1);
    }
    printf("fork %d\n", c > 0);
    n = read(p[0], buf, sizeof buf - 1);
    buf[n < 0 ? 0 : n] = 0;
    printf("heard %d %s\n", (int)n, buf);
    r = (waitpid(c, &st, 0) == c);
    printf("join %d %d %d\n", r, WIFEXITED(st), WEXITSTATUS(st));

    fflush(stdout);
    c = fork();
    if (c == 0) {
        execl("/bin/echo", "echo", "execl-ran", (char *)0);
        _exit(9);
    }
    waitpid(c, &st, 0);
    printf("execl %d %d\n", WIFEXITED(st), WEXITSTATUS(st));

    fflush(stdout);
    c = fork();
    if (c == 0) {
        char *av[] = { "echo", "execvp-ran", 0 };
        execvp("echo", av);
        _exit(9);
    }
    waitpid(c, &st, 0);
    printf("execvp %d %d\n", WIFEXITED(st), WEXITSTATUS(st));

    r = close(p[0]);
    r += close(p[1]);
    printf("done %d\n", r);
    return 0;
}
