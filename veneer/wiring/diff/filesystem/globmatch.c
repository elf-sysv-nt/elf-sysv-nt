/* filesystem slice: glob and fnmatch -- sorted matches, GLOB_NOMATCH,
   GLOB_APPEND, glob_pattern_p, and fnmatch's pathname and casefold
   flags. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <fnmatch.h>

int main(void)
{
    static const char *files[] = { "a.c", "a.h", "b.c", "note.txt" };
    char tmpl[] = "/tmp/esn-glob-XXXXXX";
    char path[256], pat[256];
    glob_t g;
    unsigned i;
    int r;

    if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); return 1; }
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        int fd;
        snprintf(path, sizeof path, "%s/%s", tmpl, files[i]);
        fd = open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0)
            close(fd);
    }

    snprintf(pat, sizeof pat, "%s/*.c", tmpl);
    r = glob(pat, 0, NULL, &g);
    printf("glob %d n %d\n", r, (int)g.gl_pathc);
    for (i = 0; i < g.gl_pathc; i++)
        printf("m %s\n", g.gl_pathv[i] + strlen(tmpl) + 1);

    snprintf(pat, sizeof pat, "%s/*.h", tmpl);
    r = glob(pat, GLOB_APPEND, NULL, &g);
    printf("append %d n %d\n", r, (int)g.gl_pathc);
    for (i = 0; i < g.gl_pathc; i++)
        printf("am %s\n", g.gl_pathv[i] + strlen(tmpl) + 1);
    globfree(&g);

    snprintf(pat, sizeof pat, "%s/*.zzz", tmpl);
    r = glob(pat, 0, NULL, &g);
    printf("nomatch %d\n", r == GLOB_NOMATCH);
    globfree(&g);

    printf("pattern-p %d %d %d\n", glob_pattern_p("a*.c", 0),
           glob_pattern_p("plain", 0), glob_pattern_p("q?x", 1));

    printf("fnm %d\n", fnmatch("*.c", "a.c", 0));
    printf("fnm-no %d\n", fnmatch("*.c", "a.h", 0) == FNM_NOMATCH);
    printf("fnm-path %d %d\n", fnmatch("*/b.c", "sub/b.c", FNM_PATHNAME),
           fnmatch("*", "sub/b.c", FNM_PATHNAME) == FNM_NOMATCH);
    printf("fnm-case %d %d\n", fnmatch("A.C", "a.c", FNM_CASEFOLD),
           fnmatch("A.C", "a.c", 0) == FNM_NOMATCH);
    printf("fnm-period %d %d\n",
           fnmatch("*", ".hidden", FNM_PERIOD) == FNM_NOMATCH,
           fnmatch(".*", ".hidden", FNM_PERIOD));
    return 0;
}
