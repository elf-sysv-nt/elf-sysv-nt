/* runtime slice: the assert surface. A passing assert is silent; a
   failing one aborts, observed as SIGABRT through a forked child
   with its message left on the real stderr, since __FILE__ differs
   between the two sides' build paths; __assert, whose arguments are
   ours to pin, dies in a child with stderr folded onto stdout and
   both program-name variables pinned so the message itself is
   compared; and re-including assert.h under NDEBUG compiles the
   failure away. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char *__progname;

static void hard(void)
{
    assert(0 && "must abort");
}

#define NDEBUG
#include <assert.h>

static void soft(void)
{
    assert(0 && "compiled away");
}

int main(void)
{
    pid_t pid;
    int st;

    assert(1 + 1 == 2);
    printf("a passing assert is silent\n");

    soft();
    printf("NDEBUG compiles the failure away\n");
    fflush(stdout);

    pid = fork();
    if (pid == 0) {
        hard();
        _exit(0);
    }
    waitpid(pid, &st, 0);
    printf("assert child: signaled %d, SIGABRT %d\n",
           WIFSIGNALED(st) ? 1 : 0,
           (WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT) ? 1 : 0);
    fflush(stdout);

    pid = fork();
    if (pid == 0) {
        program_invocation_name = (char *)"case";
        program_invocation_short_name = (char *)"case";
        __progname = (char *)"case";
        dup2(1, 2);
        __assert("two > three", "f.c", 7);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    printf("__assert child: signaled %d, SIGABRT %d\n",
           WIFSIGNALED(st) ? 1 : 0,
           (WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT) ? 1 : 0);
    return 0;
}
