/* stdlib slice: leaving -- on_exit handler order and arguments, exit
   status observed by a waiting parent, quick_exit skipping them. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static void seen(int status, void *tag)
{
    printf("on_exit %s status=%d\n", (const char *)tag, status);
}

int main(void)
{
    pid_t pid;
    int st;

    pid = fork();
    if (pid == 0) {
        on_exit(seen, (void *)"first");
        on_exit(seen, (void *)"second");
        fflush(stdout);
        exit(5);
    }
    waitpid(pid, &st, 0);
    printf("child %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);

    pid = fork();
    if (pid == 0) {
        on_exit(seen, (void *)"never");
        fflush(stdout);
        quick_exit(9);
    }
    waitpid(pid, &st, 0);
    printf("quick %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return 0;
}
