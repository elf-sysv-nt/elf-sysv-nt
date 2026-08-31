/* filesystem slice: the directory stream -- opendir, readdir, telldir,
   seekdir, rewinddir, dirfd, fdopendir, closedir; entries collected and
   sorted so readdir order never decides the outcome. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int collect(DIR *d, char names[16][64])
{
    struct dirent *e;
    char raw[16][64];
    char *ptrs[16];
    int n = 0, i;
    while ((e = readdir(d)) != NULL && n < 16) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(raw[n], 64, "%s", e->d_name);
        n++;
    }
    for (i = 0; i < n; i++)
        ptrs[i] = raw[i];
    qsort(ptrs, n, sizeof *ptrs, cmp);
    for (i = 0; i < n; i++)
        memcpy(names[i], ptrs[i], 64);
    return n;
}

int main(void)
{
    char tmpl[] = "/tmp/esn-dstream-XXXXXX";
    char path[256], names[16][64];
    DIR *d;
    long pos;
    int i, n, fd;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    for (i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/f%d", tmpl, i);
        fd = open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0)
            close(fd);
    }

    d = opendir(tmpl);
    printf("opendir %d\n", d != NULL);
    printf("dirfd %d\n", dirfd(d) >= 0);

    n = collect(d, names);
    printf("count %d\n", n);
    for (i = 0; i < n; i++)
        printf("ent %s\n", names[i]);

    rewinddir(d);
    pos = telldir(d);
    n = 0;
    while (readdir(d) != NULL)
        n++;
    printf("all-with-dots %d\n", n);
    seekdir(d, pos);
    n = 0;
    while (readdir(d) != NULL)
        n++;
    printf("after-seek %d\n", n);
    printf("closedir %d\n", closedir(d));

    fd = open(tmpl, O_RDONLY | O_DIRECTORY);
    d = fdopendir(fd);
    printf("fdopendir %d\n", d != NULL);
    n = collect(d, names);
    printf("recount %d\n", n);
    closedir(d);

    d = opendir("/tmp/esn-no-such-dir");
    printf("opendir-missing %d\n", d == NULL);
    return 0;
}
