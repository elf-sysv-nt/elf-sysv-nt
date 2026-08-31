/* process slice: posix_spawn running a real child through the shell,
   posix_spawnp resolving on PATH, the ENOENT refusal reported as a
   return value rather than through a half-born child, and a file
   action redirecting the child's stdout to /dev/null. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int main(void)
{
    posix_spawn_file_actions_t fa;
    pid_t pid;
    int rc, status;
    char *argv_exit[] = { "sh", "-c", "exit 7", NULL };
    char *argv_true[] = { "true", NULL };
    char *argv_echo[] = { "sh", "-c", "echo swallowed", NULL };

    rc = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv_exit, environ);
    waitpid(pid, &status, 0);
    printf("spawn %d %d %d\n", rc == 0, WIFEXITED(status),
           WEXITSTATUS(status) == 7);

    rc = posix_spawnp(&pid, "true", NULL, NULL, argv_true, environ);
    waitpid(pid, &status, 0);
    printf("spawnp %d %d %d\n", rc == 0, WIFEXITED(status),
           WEXITSTATUS(status) == 0);

    rc = posix_spawn(&pid, "/no/such/binary", NULL, NULL,
                     argv_true, environ);
    printf("missing %d\n", rc == ENOENT);

    rc = posix_spawn_file_actions_init(&fa);
    printf("fa-init %d\n", rc == 0);
    rc = posix_spawn_file_actions_addopen(&fa, 1, "/dev/null",
                                          O_WRONLY, 0);
    printf("fa-open %d\n", rc == 0);
    rc = posix_spawn_file_actions_adddup2(&fa, 1, 2);
    printf("fa-dup2 %d\n", rc == 0);
    rc = posix_spawn_file_actions_addclose(&fa, 47);
    printf("fa-close %d\n", rc == 0);
    rc = posix_spawn(&pid, "/bin/sh", &fa, NULL, argv_echo, environ);
    waitpid(pid, &status, 0);
    printf("fa-spawn %d %d %d\n", rc == 0, WIFEXITED(status),
           WEXITSTATUS(status) == 0);
    rc = posix_spawn_file_actions_destroy(&fa);
    printf("fa-destroy %d\n", rc == 0);
    return 0;
}
