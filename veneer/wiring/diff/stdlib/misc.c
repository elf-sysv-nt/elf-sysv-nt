/* stdlib slice: temp names, paths, subprocesses, and small answers --
   mkstemp/mkdtemp/mkstemps, realpath and canonicalize_file_name,
   system, rpmatch, getsubopt, and the C-locale multibyte no-ops. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    char tf[] = "/tmp/esn-diff-XXXXXX";
    char ts[] = "/tmp/esn-diff-XXXXXX.txt";
    char td[] = "/tmp/esn-diff-XXXXXX";
    char *p, *sub, *value;
    char opts[] = "b,a=1,zzz";
    char *tokens[] = { "a", "b", NULL };
    int fd, r;
    wchar_t wc;

    fd = mkstemp(tf);
    printf("mkstemp %d %d\n", fd >= 0, strncmp(tf, "/tmp/esn-diff-", 14) == 0);
    close(fd);

    p = realpath(tf, NULL);
    printf("realpath %d\n", p != NULL && strcmp(p, tf) == 0);
    free(p);
    unlink(tf);
    printf("realgone %d\n", realpath(tf, NULL) == NULL);

    fd = mkstemps(ts, 4);
    printf("mkstemps %d %d\n", fd >= 0,
           strcmp(ts + strlen(ts) - 4, ".txt") == 0);
    close(fd);
    unlink(ts);

    p = mkdtemp(td);
    printf("mkdtemp %d\n", p != NULL);
    sub = canonicalize_file_name(td);
    printf("canon %d\n", sub != NULL && strcmp(sub, td) == 0);
    free(sub);
    rmdir(td);

    r = system("exit 7");
    printf("system %d\n", WIFEXITED(r) ? WEXITSTATUS(r) : -1);
    printf("shell %d\n", system(NULL) != 0);

    printf("rpmatch %d %d %d\n", rpmatch("yes"), rpmatch("NO"), rpmatch("?"));

    sub = opts;
    r = getsubopt(&sub, tokens, &value);
    printf("subopt1 %d %d\n", r, value == NULL);
    r = getsubopt(&sub, tokens, &value);
    printf("subopt2 %d %s\n", r, value);
    r = getsubopt(&sub, tokens, &value);
    printf("subopt3 %d\n", r);

    printf("mblen %d %d\n", mblen("A", 1), mblen("", 1));
    printf("mbtowc %d %d\n", mbtowc(&wc, "B", 1), wc == L'B');
    return 0;
}
