/* filesystem slice: walking a tree -- ftw, nftw, and the fts family
   over one constructed tree; each walk's findings are collected and
   sorted so traversal order never decides the outcome. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ftw.h>
#include <fts.h>
#include <sys/stat.h>

static char base[64];
static char seen[32][128];
static int nseen;

static void note(const char *path, const char *kind)
{
    const char *rel = path + strlen(base);
    if (*rel == '/')
        rel++;
    if (*rel == '\0')
        rel = ".";
    snprintf(seen[nseen], sizeof seen[0], "%s %s", rel, kind);
    nseen++;
}

static int cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void flush(const char *tag)
{
    int i;
    qsort(seen, nseen, sizeof seen[0], cmp);
    for (i = 0; i < nseen; i++)
        printf("%s %s\n", tag, seen[i]);
    nseen = 0;
}

static int on_ftw(const char *p, const struct stat *st, int flag)
{
    (void)st;
    note(p, flag == FTW_D ? "d" : "f");
    return 0;
}

static int on_nftw(const char *p, const struct stat *st, int flag,
                   struct FTW *ctx)
{
    char kind[32];
    (void)st;
    snprintf(kind, sizeof kind, "%s lev%d",
             flag == FTW_D ? "d" : flag == FTW_DP ? "dp" : "f",
             ctx->level);
    note(p, kind);
    return 0;
}

int main(void)
{
    char tmpl[] = "/tmp/esn-walk-XXXXXX";
    char path[256];
    static const char *dirs[] = { "d1", "d1/d2" };
    static const char *files[] = { "top", "d1/mid", "d1/d2/leaf" };
    FTS *fts;
    FTSENT *e;
    char *argv[2];
    unsigned i;
    int r;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    snprintf(base, sizeof base, "%s", tmpl);
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", tmpl, dirs[i]);
        mkdir(path, 0755);
    }
    for (i = 0; i < 3; i++) {
        int fd;
        snprintf(path, sizeof path, "%s/%s", tmpl, files[i]);
        fd = open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0)
            close(fd);
    }

    r = ftw(tmpl, on_ftw, 8);
    printf("ftw %d\n", r);
    flush("ftw");

    r = nftw(tmpl, on_nftw, 8, FTW_DEPTH | FTW_PHYS);
    printf("nftw %d\n", r);
    flush("nftw");

    argv[0] = tmpl;
    argv[1] = NULL;
    fts = fts_open(argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    printf("fts-open %d\n", fts != NULL);
    while ((e = fts_read(fts)) != NULL) {
        if (e->fts_info == FTS_D)
            note(e->fts_path, "pre");
        else if (e->fts_info == FTS_DP)
            note(e->fts_path, "post");
        else if (e->fts_info == FTS_F)
            note(e->fts_path, "file");
    }
    r = fts_close(fts);
    printf("fts-close %d\n", r);
    flush("fts");
    return 0;
}
