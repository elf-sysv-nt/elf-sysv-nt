/* filesystem slice: scandir with alphasort and versionsort, a custom
   filter, and scandirat -- the orderings are the observable fact. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

static int only_f(const struct dirent *e)
{
    return e->d_name[0] == 'f';
}

static void show(const char *tag, struct dirent **list, int n)
{
    int i;
    printf("%s %d:", tag, n);
    for (i = 0; i < n; i++)
        printf(" %s", list[i]->d_name);
    printf("\n");
    for (i = 0; i < n; i++)
        free(list[i]);
    free(list);
}

int main(void)
{
    static const char *files[] = {
        "file1", "file10", "file2", "file20", "file3", "g-side", "other"
    };
    char tmpl[] = "/tmp/esn-scan-XXXXXX";
    char path[256];
    struct dirent **list;
    unsigned i;
    int n, dfd;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        int fd;
        snprintf(path, sizeof path, "%s/%s", tmpl, files[i]);
        fd = open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0)
            close(fd);
    }

    n = scandir(tmpl, &list, only_f, alphasort);
    show("alpha", list, n);

    n = scandir(tmpl, &list, only_f, versionsort);
    show("version", list, n);

    n = scandir(tmpl, &list, NULL, alphasort);
    printf("unfiltered-has-dots %d\n", n == 9);
    while (n-- > 0)
        free(list[n]);
    free(list);

    dfd = open(tmpl, O_RDONLY | O_DIRECTORY);
    n = scandirat(dfd, ".", &list, only_f, versionsort);
    show("at-version", list, n);
    close(dfd);

    n = scandir("/tmp/esn-no-such-dir", &list, NULL, alphasort);
    printf("scandir-missing %d\n", n);
    return 0;
}
