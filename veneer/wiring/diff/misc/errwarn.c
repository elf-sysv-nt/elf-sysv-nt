/* misc slice: the err/warn reporters and error(3). Both name the
   program after argv[0], which differs between the two sides' build
   paths, so the case pins program_invocation_name, its short twin,
   and the __progname pair -- err reads the latter, error the former
   -- to a fixed string first; stderr is folded onto stdout so the
   messages are observable. The exiting forms run in forked children
   and their statuses come back through wait. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <error.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

extern char *__progname;
extern char *__progname_full;

static void vwarn_call(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

static void vwarnx_call(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

int main(void)
{
    pid_t pid;
    int st;

    program_invocation_name = (char *)"case";
    program_invocation_short_name = (char *)"case";
    __progname = (char *)"case";
    __progname_full = (char *)"case";
    dup2(1, 2);

    errno = ENOENT;
    warn("open %s", "f.txt");
    warnx("plain %d", 7);
    errno = EACCES;
    vwarn_call("via %s", "vwarn");
    vwarnx_call("via %s", "vwarnx");

    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        errno = EPERM;
        err(3, "boom %s", "now");
    }
    waitpid(pid, &st, 0);
    printf("err %d %d\n", WIFEXITED(st), WEXITSTATUS(st));

    fflush(stdout);
    pid = fork();
    if (pid == 0)
        errx(4, "dry %s", "boom");
    waitpid(pid, &st, 0);
    printf("errx %d %d\n", WIFEXITED(st), WEXITSTATUS(st));

    error(0, 0, "bare %d", 1);
    error(0, ENOENT, "carried %d", 2);
    printf("count %u\n", error_message_count);

    error_one_per_line = 1;
    error_at_line(0, 0, "file.c", 10, "once");
    error_at_line(0, 0, "file.c", 10, "twice same spot");
    error_at_line(0, 0, "file.c", 11, "new line");
    printf("perline %u\n", error_message_count);

    fflush(stdout);
    pid = fork();
    if (pid == 0)
        error(5, EINVAL, "fatal");
    waitpid(pid, &st, 0);
    printf("errorexit %d %d\n", WIFEXITED(st), WEXITSTATUS(st));

    return 0;
}
